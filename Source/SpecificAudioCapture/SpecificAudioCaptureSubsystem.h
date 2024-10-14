// Copyright HTC Corporation. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "IPAddress.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Common/UdpSocketSender.h"
#include "SpecificAudioCaptureSubsystem.generated.h"

class USpecificAudioCaptureComponent;

USTRUCT()
struct FSpecificAudioCaptureStreams
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<USpecificAudioCaptureComponent *> streams;
};

//DECLARE_LOG_CATEGORY_EXTERN(LogViveCustomHandGesture, Log, All);
UCLASS()
class SPECIFICAUDIOCAPTURE_API USpecificAudioCaptureSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	USpecificAudioCaptureSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterSpecificAudioCapture(USpecificAudioCaptureComponent* component);
	void UnRegisterSpecificAudioCapture(USpecificAudioCaptureComponent* component);

	void SendNetworkAudio(FName name, int32 sampleRate, int32 channelNum, TArray<float>& data);

private:
	UPROPERTY()
	TMap<FName, FSpecificAudioCaptureStreams> components;

	FSocket* Socket;
	FUdpSocketReceiver* UDPReceiver;
	bool IsServer;

	FCriticalSection NetworkCriticalSection;
	TSharedPtr<FInternetAddr> MulticastAddr;
	TArray<uint8> networkBuffer;
};
