// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Audio.h"
#include "AudioAnalytics.h"
#include "AudioCapture.h"
#include "AudioCaptureDeviceInterface.h"
#include "Components/SynthComponent.h"
#include "SpecificAudioCaptureComponent.generated.h"

namespace Audio {
	class SPECIFICAUDIOCAPTURE_API FSpecificAudioCaptureSynth
	{
	public:
		FSpecificAudioCaptureSynth();
		virtual ~FSpecificAudioCaptureSynth();

		// Gets the default capture device info
		bool GetSpecificCaptureDeviceInfo(FString name, FCaptureDeviceInfo& OutInfo, int32 &deviceIndex);

		// Opens up a stream to the default capture device
		bool OpenSpecificStream(int32 deviceIndex = -1);

		// Starts capturing audio
		bool StartCapturing();

		// Stops capturing audio
		void StopCapturing();

		// Immediately stop capturing audio
		void AbortCapturing();

		// Returned if the capture synth is closed
		bool IsStreamOpen() const;

		// Returns true if the capture synth is capturing audio
		bool IsCapturing() const;

		// Retrieves audio data from the capture synth.
		// This returns audio only if there was non-zero audio since this function was last called.
		bool GetAudioData(TArray<float>& OutAudioData);

		// Returns the number of samples enqueued in the capture synth
		int32 GetNumSamplesEnqueued();

		void InitNetwork();
		void AddAudioData(const TArray<float>& InAudioData);
		class USpecificAudioCaptureComponent* parent;
	private:

		// Number of samples enqueued
		int32 NumSamplesEnqueued;

		// Information about the default capture device we're going to use
		FCaptureDeviceInfo CaptureInfo;

		// Audio capture object dealing with getting audio callbacks
		FAudioCapture AudioCapture;

		// Critical section to prevent reading and writing from the captured buffer at the same time
		FCriticalSection CaptureCriticalSection;

		// Buffer of audio capture data, yet to be copied to the output 
		TArray<float> AudioCaptureData;


		// If the object has been initialized
		bool bInitialized;

		// If we're capturing data
		
	public:
		bool bIsCapturing;
		bool bStreamingOverride;
	};
}

UCLASS(ClassGroup = Synth, meta = (BlueprintSpawnableComponent))
class SPECIFICAUDIOCAPTURE_API USpecificAudioCaptureComponent : public USynthComponent
{
	GENERATED_BODY()

protected:

	USpecificAudioCaptureComponent(const FObjectInitializer& ObjectInitializer);

	
	bool OpenStream();

	//~ Begin USynthComponent interface
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
	virtual void OnBeginGenerate() override;
	virtual void OnEndGenerate() override;
	//~ End USynthComponent interface

	//~ Begin UObject interface
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;
	virtual void FinishDestroy() override;
	//~ End UObject interface
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Device")
	FString DeviceInputName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Device")
	FName StreamName;

	void OnData(const void* AudioData, int32 NumFrames, int32 NumChannels, int32 SampleRate);
private:
	friend class USpecificAudioCaptureSubsystem;

	Audio::FSpecificAudioCaptureSynth CaptureSynth;
	TArray<float> CaptureAudioData;
	int32 CapturedAudioDataSamples;

	bool bSuccessfullyInitialized;
	bool bIsCapturing;
	bool bIsStreamOpen;
	int32 CaptureChannels;
	int32 FramesSinceStarting;
	int32 ReadSampleIndex;
	FThreadSafeBool bIsDestroying;
	FThreadSafeBool bIsNotReadyForForFinishDestroy;

	int32 NetworkSamplRate;

	int32 deviceIndex;
};