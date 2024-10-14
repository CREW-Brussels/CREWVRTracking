// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpecificAudioCaptureComponent.h"
#include "SpecificAudioCaptureSubsystem.h"


static const unsigned int MaxBufferSize = 2 * 5 * 48000;

USpecificAudioCaptureComponent::USpecificAudioCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSuccessfullyInitialized = false;
	bIsCapturing = false;
	CapturedAudioDataSamples = 0;
	ReadSampleIndex = 0;
	bIsDestroying = false;
	bIsNotReadyForForFinishDestroy = false;
	bIsStreamOpen = false;
	CaptureAudioData.Reserve(2 * 48000 * 5);
	deviceIndex = -1;
	NetworkSamplRate = 48000;
	CaptureSynth.parent = this;
}

bool USpecificAudioCaptureComponent::OpenStream()
{
	Audio::FCaptureDeviceInfo DeviceInfo;
	deviceIndex = -1;
	if (CaptureSynth.GetSpecificCaptureDeviceInfo(DeviceInputName, DeviceInfo, deviceIndex))
	{
		if (DeviceInfo.PreferredSampleRate > 0)
		{
			//SampleRate = DeviceInfo.PreferredSampleRate;
		}
		else
		{
			UE_LOG(LogAudio, Warning, TEXT("Attempted to open a capture device with the invalid SampleRate value of %i"), DeviceInfo.PreferredSampleRate);
		}
		NumChannels = DeviceInfo.InputChannels;

		if (NumChannels > 0 && NumChannels <= 8)
		{
			// This may fail if capture synths aren't supported on a given platform or if something went wrong with the capture device
			bIsStreamOpen = CaptureSynth.OpenSpecificStream(deviceIndex);
			CaptureSynth.StartCapturing();

			Audio::Analytics::RecordEvent_Usage(TEXT("AudioCapture.AudioCaptureComponentInitialized"));

			return true;
		}
		else
		{
			UE_LOG(LogAudio, Warning, TEXT("Attempted to open a device with the invalid NumChannels value of %i"), NumChannels);
		}
	}
	return false;
}

bool USpecificAudioCaptureComponent::Init(int32& SampleRate)
{
	SampleRate = NetworkSamplRate;
	return true;
}

void USpecificAudioCaptureComponent::BeginPlay()
{
	if (GetOwner() && GetOwner()->GetGameInstance()) {
		USpecificAudioCaptureSubsystem* system = GetOwner()->GetGameInstance()->GetSubsystem<USpecificAudioCaptureSubsystem>();
		if (system) {
			system->RegisterSpecificAudioCapture(this);
		}
	}
}

void USpecificAudioCaptureComponent::BeginDestroy()
{
	/*if (GetOwner() && GetOwner()->GetGameInstance()) {
		USpecificAudioCaptureSubsystem* system = GetOwner()->GetGameInstance()->GetSubsystem<USpecificAudioCaptureSubsystem>();
		if (system) {
			system->UnRegisterSpecificAudioCapture(this);
		}
	}*/
	Super::BeginDestroy();

	// Flag that we're beginning to be destroyed
	// This is so that if a mic capture is open, we shut it down on the render thread
	bIsDestroying = true;

	CaptureSynth.bIsCapturing = false;

	// Make sure stop is kicked off
	Stop();
}

bool USpecificAudioCaptureComponent::IsReadyForFinishDestroy()
{
	//Prevent an infinite loop here if it was marked pending kill while generating
	OnEndGenerate();

	return !bIsNotReadyForForFinishDestroy;
}

void USpecificAudioCaptureComponent::FinishDestroy()
{
	if (CaptureSynth.IsStreamOpen())
	{
		CaptureSynth.AbortCapturing();
	}

	check(!CaptureSynth.IsStreamOpen());

	Super::FinishDestroy();
	bSuccessfullyInitialized = false;
	bIsCapturing = false;
	bIsDestroying = false;
	bIsStreamOpen = false;
}

void USpecificAudioCaptureComponent::OnBeginGenerate()
{
	CapturedAudioDataSamples = 0;
	ReadSampleIndex = 0;
	CaptureAudioData.Reset();

	if (!bIsStreamOpen)
	{
		bIsStreamOpen = CaptureSynth.OpenSpecificStream(deviceIndex);
	}

	if (bIsStreamOpen)
	{
		CaptureSynth.StartCapturing();
		check(CaptureSynth.IsCapturing());

		// Don't allow this component to be destroyed until the stream is closed again
		bIsNotReadyForForFinishDestroy = true;
		FramesSinceStarting = 0;
		ReadSampleIndex = 0;
	}

}

void USpecificAudioCaptureComponent::OnEndGenerate()
{
	if (bIsStreamOpen)
	{
		check(CaptureSynth.IsStreamOpen());
		CaptureSynth.StopCapturing();
		bIsStreamOpen = false;

		bIsNotReadyForForFinishDestroy = false;
	}
}

int32 USpecificAudioCaptureComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	// Don't do anything if the stream isn't open
	if (!bIsStreamOpen || !CaptureSynth.IsStreamOpen() || !CaptureSynth.IsCapturing())
	{
		// Just return NumSamples, which uses zero'd buffer
		return NumSamples;
	}
	int32 OutputSamplesGenerated = 0;
	//In case of severe overflow, just drop the data
	if (CaptureAudioData.Num() > MaxBufferSize)
	{
		//Clear the CaptureSynth's data, too
		CaptureSynth.GetAudioData(CaptureAudioData);
		CaptureAudioData.Reset();
		return NumSamples;
	}
	if (CapturedAudioDataSamples > 0 || CaptureSynth.GetNumSamplesEnqueued() > 1024)
	{
		// Check if we need to read more audio data from capture synth
		if (ReadSampleIndex + NumSamples > CaptureAudioData.Num())
		{
			// but before we do, copy off the remainder of the capture audio data buffer if there's data in it
			int32 SamplesLeft = FMath::Max(0, CaptureAudioData.Num() - ReadSampleIndex);
			if (SamplesLeft > 0)
			{
				float* CaptureDataPtr = CaptureAudioData.GetData();
				if (!(ReadSampleIndex + NumSamples > MaxBufferSize - 1))
				{
					FMemory::Memcpy(OutAudio, &CaptureDataPtr[ReadSampleIndex], SamplesLeft * sizeof(float));
				}
				else
				{
					UE_LOG(LogAudio, Warning, TEXT("Attempt to write past end of buffer in OnGenerateAudio, when we got more data from the synth"));
					return NumSamples;
				}
				// Track samples generated
				OutputSamplesGenerated += SamplesLeft;
			}
			// Get another block of audio from the capture synth
			CaptureAudioData.Reset();
			CaptureSynth.GetAudioData(CaptureAudioData);
			// Reset the read sample index since we got a new buffer of audio data
			ReadSampleIndex = 0;
		}
		// note it's possible we didn't get any more audio in our last attempt to get it
		if (CaptureAudioData.Num() > 0)
		{
			// Compute samples to copy
			int32 NumSamplesToCopy = FMath::Min(NumSamples - OutputSamplesGenerated, CaptureAudioData.Num() - ReadSampleIndex);
			float* CaptureDataPtr = CaptureAudioData.GetData();
			if (!(ReadSampleIndex + NumSamplesToCopy > MaxBufferSize - 1))
			{
				FMemory::Memcpy(&OutAudio[OutputSamplesGenerated], &CaptureDataPtr[ReadSampleIndex], NumSamplesToCopy * sizeof(float));
			}
			else
			{
				UE_LOG(LogAudio, Warning, TEXT("Attempt to read past end of buffer in OnGenerateAudio, when we did not get more data from the synth"));
				return NumSamples;
			}
			ReadSampleIndex += NumSamplesToCopy;
			OutputSamplesGenerated += NumSamplesToCopy;
		}
		CapturedAudioDataSamples += OutputSamplesGenerated;
	}
	else
	{
		// Say we generated the full samples, this will result in silence
		OutputSamplesGenerated = NumSamples;
	}
	return OutputSamplesGenerated;
}

void USpecificAudioCaptureComponent::OnData(const void* AudioData, int32 NumFrames, int32 In_NumChannels, int32 SampleRate) {
	if (GetOwner() && GetOwner()->GetGameInstance()) {
		USpecificAudioCaptureSubsystem* system = GetOwner()->GetGameInstance()->GetSubsystem<USpecificAudioCaptureSubsystem>();
		if (system) {
			CaptureAudioData.Empty();
			int32 NumSamples = In_NumChannels * NumFrames;
			int32 Index = CaptureAudioData.AddUninitialized(NumSamples);
			float* AudioCaptureDataPtr = CaptureAudioData.GetData();

			//Avoid reading outside of buffer boundaries
			if (!(NumSamples > MaxBufferSize))
			{
				FMemory::Memcpy(&AudioCaptureDataPtr[Index], AudioData, NumSamples * sizeof(float));
				system->SendNetworkAudio(StreamName, SampleRate, In_NumChannels, CaptureAudioData);
			}

			//system->SendNetworkAudio(StreamName, SampleRate, In_NumChannels, CaptureAudioData);
		}
	}
}

namespace Audio {
	FSpecificAudioCaptureSynth::FSpecificAudioCaptureSynth()
		: NumSamplesEnqueued(0)
		, bInitialized(false)
		, bIsCapturing(false)
	{
		bStreamingOverride = false;
		parent = nullptr;
	}

	FSpecificAudioCaptureSynth::~FSpecificAudioCaptureSynth()
	{
	}

	bool FSpecificAudioCaptureSynth::GetSpecificCaptureDeviceInfo(FString name, FCaptureDeviceInfo& OutInfo, int32 &deviceIndex)
	{
		TArray<FCaptureDeviceInfo> devices;
		int deviceCount = AudioCapture.GetCaptureDevicesAvailable(devices);
		deviceIndex = -1;
		for (int i = 0; i < devices.Num(); i++) {
			UE_LOG(LogAudio, Warning, TEXT("%s"), *devices[i].DeviceName);
			if (devices[i].DeviceName.StartsWith(name)) {
				OutInfo = devices[i];
				deviceIndex = i;
				return true;
			}
		}
		return AudioCapture.GetCaptureDeviceInfo(OutInfo);
	}

	bool FSpecificAudioCaptureSynth::OpenSpecificStream(int32 deviceIndex)
	{
		bool bSuccess = true;
		if (!AudioCapture.IsStreamOpen())
		{
			FOnAudioCaptureFunction OnCapture = [this](const void* AudioData, int32 NumFrames, int32 NumChannels, int32 SampleRate, double StreamTime, bool bOverFlow)
				{
					int32 NumSamples = NumChannels * NumFrames;

					FScopeLock Lock(&CaptureCriticalSection);

					if (bIsCapturing)
					{
						if (parent != nullptr) {
							parent->OnData(AudioData, NumFrames, NumChannels, SampleRate);
						}
						/*
						// Append the audio memory to the capture data buffer
						int32 Index = AudioCaptureData.AddUninitialized(NumSamples);
						float* AudioCaptureDataPtr = AudioCaptureData.GetData();

						//Avoid reading outside of buffer boundaries
						if (!(AudioCaptureData.Num() + NumSamples > MaxBufferSize))
						{
							FMemory::Memcpy(&AudioCaptureDataPtr[Index], AudioData, NumSamples * sizeof(float));
						}
						else
						{
							UE_LOG(LogAudio, Warning, TEXT("Attempt to write past end of buffer in OpenDefaultStream [%u]"), AudioCaptureData.Num() + NumSamples);
						}
						*/
					}
				};

			// Prepare the audio buffer memory for 2 seconds of stereo audio at 48k SR to reduce chance for allocation in callbacks
			AudioCaptureData.Reserve(2 * 2 * 48000);

			FAudioCaptureDeviceParams Params = FAudioCaptureDeviceParams();
			Params.DeviceIndex = deviceIndex;

			// Start the stream here to avoid hitching the audio render thread. 
			if (AudioCapture.OpenAudioCaptureStream(Params, MoveTemp(OnCapture), 1024))
			{
				AudioCapture.StartStream();
			}
			else
			{
				bSuccess = false;
			}
		}
		return bSuccess;
	}

	bool FSpecificAudioCaptureSynth::StartCapturing()
	{
		FScopeLock Lock(&CaptureCriticalSection);

		AudioCaptureData.Reset();

		//check(AudioCapture.IsStreamOpen());

		bIsCapturing = true;
		return true;
	}

	void FSpecificAudioCaptureSynth::StopCapturing()
	{
		//check(AudioCapture.IsStreamOpen());
		//check(AudioCapture.IsCapturing());
		FScopeLock Lock(&CaptureCriticalSection);
		bIsCapturing = false;
	}

	void FSpecificAudioCaptureSynth::AbortCapturing()
	{
		AudioCapture.AbortStream();
		AudioCapture.CloseStream();
	}

	bool FSpecificAudioCaptureSynth::IsStreamOpen() const
	{
		return AudioCapture.IsStreamOpen() || bStreamingOverride;
	}

	bool FSpecificAudioCaptureSynth::IsCapturing() const
	{
		return bIsCapturing;
	}

	int32 FSpecificAudioCaptureSynth::GetNumSamplesEnqueued()
	{
		FScopeLock Lock(&CaptureCriticalSection);
		return AudioCaptureData.Num();
	}

	bool FSpecificAudioCaptureSynth::GetAudioData(TArray<float>& OutAudioData)
	{
		FScopeLock Lock(&CaptureCriticalSection);

		int32 CaptureDataSamples = AudioCaptureData.Num();
		if (CaptureDataSamples > 0)
		{
			// Append the capture audio to the output buffer
			int32 OutIndex = OutAudioData.AddUninitialized(CaptureDataSamples);
			float* OutDataPtr = OutAudioData.GetData();

			//Check bounds of buffer
			if (!(OutIndex > MaxBufferSize))
			{
				FMemory::Memcpy(&OutDataPtr[OutIndex], AudioCaptureData.GetData(), CaptureDataSamples * sizeof(float));
			}
			else
			{
				UE_LOG(LogAudio, Warning, TEXT("Attempt to write past end of buffer in GetAudioData"));
				return false;
			}

			// Reset the capture data buffer since we copied the audio out
			AudioCaptureData.Reset();
			return true;
		}
		return false;
	}

	void FSpecificAudioCaptureSynth::AddAudioData(const TArray<float> &InAudioData)
	{
		FScopeLock Lock(&CaptureCriticalSection);
		if (!(AudioCaptureData.Num() + InAudioData.Num() > MaxBufferSize)) {
			AudioCaptureData.Append(InAudioData);
		}
		else {
			UE_LOG(LogAudio, Warning, TEXT("Attempt to write past end of buffer in AddAudioData [%u]"), AudioCaptureData.Num() + InAudioData.Num());
		}
	}

	void FSpecificAudioCaptureSynth::InitNetwork() {
		bIsCapturing = true;
		bStreamingOverride = true;
	}
}

