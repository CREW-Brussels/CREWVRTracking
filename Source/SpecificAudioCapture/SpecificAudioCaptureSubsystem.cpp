// Copyright HTC Corporation. All Rights Reserved.

#include "SpecificAudioCaptureSubsystem.h"
#include "SpecificAudioCaptureComponent.h"
#include "Engine/NetDriver.h"

USpecificAudioCaptureSubsystem::USpecificAudioCaptureSubsystem() {
	Socket = nullptr;
	IsServer = false;
	UDPReceiver = nullptr;
}

void USpecificAudioCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
	MulticastAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	MulticastAddr->SetIp(FIPv4Address(255,255,255,255).Value);
	MulticastAddr->SetPort(16501);
	//Socket = FUdpSocketBuilder("AudioReceiver").AsReusable().WithMulticastTtl(1);
	//Socket->JoinMulticastGroup(*MulticastAddr);
	if (GetGameInstance()->GetWorld()->WorldType == EWorldType::PIE) { // TEMPORARY
		Socket = FUdpSocketBuilder(TEXT("UDPBroadcaster"))
			.AsReusable()
			.AsNonBlocking()
			.WithBroadcast()
			.BoundToEndpoint(FIPv4Endpoint(FIPv4Address::Any, 16501))
			.WithSendBufferSize(2 * 1024 * 1024);
		IsServer = true;
	}
	else {
		Socket = FUdpSocketBuilder(TEXT("UDPReceiver"))
			.AsNonBlocking()
			.AsReusable()
			.BoundToEndpoint(FIPv4Endpoint(FIPv4Address::Any, 16501))
			.WithReceiveBufferSize(2 * 1024 * 1024);
		FTimespan ThreadWaitTime = FTimespan::FromMilliseconds(100);
		FString ThreadName = FString::Printf(TEXT("UDP AudioReceiver"));
		UDPReceiver = new FUdpSocketReceiver(Socket, ThreadWaitTime, *ThreadName);

		UDPReceiver->OnDataReceived().BindLambda([this](const FArrayReaderPtr& DataPtr, const FIPv4Endpoint& Endpoint)
			{
				FScopeLock Lock(&NetworkCriticalSection);
				FName name;
				int32 sampleRate, channelCount;
				TArray<float> data;
				
				FMemoryReader MemoryReader(*DataPtr);

				MemoryReader << name;
				MemoryReader << sampleRate;
				MemoryReader << channelCount;
				MemoryReader << data;

				FSpecificAudioCaptureStreams* found = components.Find(name);

				if (found != nullptr) {
					for (int i = 0; i < found->streams.Num(); i++) {
						if (!found->streams[i]->bIsStreamOpen) {
							found->streams[i]->bIsStreamOpen = true;
							AsyncTask(ENamedThreads::GameThread, [found, i, sampleRate, channelCount]()
								{
									found->streams[i]->NumChannels = channelCount;
									found->streams[i]->NetworkSamplRate = sampleRate;
									found->streams[i]->CaptureSynth.InitNetwork();
									found->streams[i]->Start();
								});
							
						}
						found->streams[i]->CaptureSynth.AddAudioData(data);
					}
				}
			});

		UDPReceiver->Start();
	}
}

void USpecificAudioCaptureSubsystem::SendNetworkAudio(FName name, int32 sampleRate, int32 channelNum, TArray<float>& data) {
	FScopeLock Lock(&NetworkCriticalSection);
	networkBuffer.Empty();
	FMemoryWriter MemoryWriter(networkBuffer, true);
	MemoryWriter << name;
	MemoryWriter << sampleRate;
	MemoryWriter << channelNum;
	MemoryWriter << data;
	int32 byteSent;
	Socket->SendTo(networkBuffer.GetData(), networkBuffer.Num(), byteSent, *MulticastAddr);
}

void USpecificAudioCaptureSubsystem::Deinitialize() {
	if (UDPReceiver) {
		UDPReceiver->Stop();
		delete UDPReceiver;
		UDPReceiver = nullptr;
	}
	if (Socket) {
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void USpecificAudioCaptureSubsystem::RegisterSpecificAudioCapture(USpecificAudioCaptureComponent* component) {
	FScopeLock Lock(&NetworkCriticalSection);
	FSpecificAudioCaptureStreams *found = components.Find(component->StreamName);
	if (found != nullptr) {
		if (!IsServer) {
			found->streams.Add(component);
		}
	}
	else {
		FSpecificAudioCaptureStreams newStreams;
		newStreams.streams.Add(component);
		components.Add(component->StreamName, newStreams);
		if (IsServer) {
			component->OpenStream();
		}
	}
}

void USpecificAudioCaptureSubsystem::UnRegisterSpecificAudioCapture(USpecificAudioCaptureComponent* component) {
	FScopeLock Lock(&NetworkCriticalSection);
	FSpecificAudioCaptureStreams* found = components.Find(component->StreamName);
	if (found != nullptr) {
		found->streams.Remove(component);
		if (found->streams.Num() == 0)
		{
			components.Remove(component->StreamName);
		}
	}
}
