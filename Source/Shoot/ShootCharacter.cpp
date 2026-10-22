 // Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootCharacter.h"
#include "Engine/LocalPlayer.h"
#include "InventoryWidget.h"
#include "UI/ShopWidget.h"
#include "Machine/CookingMachine.h"
#include "Machine/ServingCounter.h"
#include "Machine/AttractCounter.h"
#include "NPC/GuestCharacter.h"
#include "Dialogue/DialogueComponent.h"
#include "Item/Plant.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Shoot.h"
#include "Animation/AnimMontage.h"
#include "Animation/ShootAnimInstance.h"
#include "Components/PostProcessComponent.h"
#include "Game/BaseGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Data/ItemActorInfo.h"


 void AShootCharacter::PlayPickMontage(FName SelectionName)
 {
 	if (CharacterActionState == ECharacterActionState::EAS_Unoccupied)
 	{
 		UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
 		if (AnimInstance && PickMontage)
 		{
 			AnimInstance->Montage_Play(PickMontage);
 			AnimInstance->Montage_JumpToSection(SelectionName,PickMontage);
 		}
 	}
 }

 bool AShootCharacter::CanAttack()
{
	return CharacterActionState == ECharacterActionState::EAS_Unoccupied &&
			CharacterState != ECharacterState::ECS_Holding &&
			CharacterState != ECharacterState::ECS_Repairing ;
}

bool AShootCharacter::IsHolding()
{
	return CharacterState == ECharacterState::ECS_Holding;
}

 bool AShootCharacter::CanPick()
 {
 	if(OverlappingItem == nullptr)
 	{
 		return false;
 	}
 	if(OverlappingItem != nullptr && OverlappingItem->ItemType == EItemType::EIT_Desk)
 	{
 		return false;
 	}
 	return true;
 }

 bool AShootCharacter::IsSpawner()
 {
 	if(OverlappingItem == nullptr)
 	{
 		return false;
 	}
 	if(OverlappingItem != nullptr && OverlappingItem->ItemType == EItemType::EIT_Spawn)
 	{
 		return true;
 	}
 	return false;
 }

 bool AShootCharacter::IsSpawnOther()
 {
 	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::White, FString::Printf(TEXT("切好菜了，生成一个对应切换的item %d"),CookingMachine->FoodActors.Num()));
 	for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 	{
 		AItemActor* CurrentActor = CookingMachine->FoodActors[i];
 		if (CurrentActor->ItemType == EItemType::EIT_SpawnOther)
 		{
 			return true;
 		}
 	}
 	return false;
 }

 bool AShootCharacter::NeedDropDown()
 {
 	return PickingItem != nullptr;
 }

 bool AShootCharacter::CanThrowToMachine()
 {
	return CharacterState == ECharacterState::ECS_Holding && CookingMachine!= nullptr &&
			CharacterState != ECharacterState::ECS_Repairing &&
				PickingItem->ItemType != EItemType::EIT_Dish;
 }

 bool AShootCharacter::NeedRepairMachine()
 {
	return CharacterState == ECharacterState::ECS_Idle && CookingMachine!= nullptr && CookingMachine->OnFire;
 }

 bool AShootCharacter::IsRepairing()
 {
	return CharacterState == ECharacterState::ECS_Repairing;
 }

 bool AShootCharacter::MachineCookFinished()
 {
 // 	if (PickingItem == nullptr || PickingItem->ItemType != EItemType::EIT_Dish)
 // 	{
 // 		return false;	
 // 	}
	// return CharacterState == ECharacterState::ECS_Holding && CookingMachine!= nullptr && CookingMachine->IsFinish;

 	//快速验证
 	return CookingMachine!= nullptr && CookingMachine->IsFinish;
 }

 bool AShootCharacter::CanGiveDishToGuest()
 {
 	if (PickingItem == nullptr || OverlappingItem == nullptr || CharacterState != ECharacterState::ECS_Holding)
 	{
 		return false;	
 	}
 	if (PickingItem->FoodIdArray.Num()==0)
 	{
 		return false;	 	}
 	if (OverlappingItem->ItemType != EItemType::EIT_Desk)
 	{
 		return false;	
 	}
 	return true;
 }

 AShootCharacter::AShootCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = false; // 镜头不与被遮挡的场景物件发生碰撞拉近距离

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

 	RetargetMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RetargetMesh"));
 	RetargetMesh->SetupAttachment(GetMesh());

 
}

void AShootCharacter::SetOverlappingItem(AItemActor* Item)
{
 	if (IsPicking)
 	{
 		return ;
 	}

 	if (OverlappingItem!= nullptr && Item != nullptr && OverlappingItem != Item)
 	{
 		OverlappingItem->SetItemState(EItemState::EIS_Idle);
 	}
	OverlappingItem = Item;
}

 void AShootCharacter::SetCookMachineItem(ACookingMachine* Item)
{
	CookingMachine = Item;
}

void AShootCharacter::SetOverlappingPlant(APlant* Plant)
{
	OverlappingPlant = Plant;
}

void AShootCharacter::SetOverlappingCounter(AServingCounter* Counter)
{
	OverlappingCounter = Counter;
}

void AShootCharacter::SetOverlappingAttractCounter(AAttractCounter* Counter)
{
	OverlappingAttractCounter = Counter;
}

void AShootCharacter::SetOverlappingGuest(AGuestCharacter* Guest)
{
	OverlappingGuest = Guest;
}

void AShootCharacter::SetOverlappingDialogue(UInteractionDialogueComponent* Comp)
{
	OverlappingDialogue = Comp;
}

void AShootCharacter::InteractWithPlant()
{
	if (!OverlappingPlant)
	{
		return;
	}

	// 上下文动作：优先处理害虫，其次收获，最后浇水
	if (OverlappingPlant->HasPest())
	{
		OverlappingPlant->RemovePest();
	}
	else if (OverlappingPlant->IsFruiting())
	{
		OverlappingPlant->Harvest();
	}
	else
	{
		OverlappingPlant->Water();
	}
}

void AShootCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AShootCharacter::Move);

		// 保留左右旋转（Yaw），禁止上下旋转（Pitch）
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AShootCharacter::Look);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AShootCharacter::Look);

		// Attack
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Triggered, this, &AShootCharacter::Attack);

		// Pick：拾取；靠近植物时 Pick 内部会转为浇水/驱虫/收获（见 AShootCharacter::Pick）
		EnhancedInputComponent->BindAction(PickAction, ETriggerEvent::Triggered, this, &AShootCharacter::Pick);

		//PushFood
		EnhancedInputComponent->BindAction(PushFoodAction, ETriggerEvent::Started, this, &AShootCharacter::PushFoodStart);
		EnhancedInputComponent->BindAction(PushFoodAction, ETriggerEvent::Completed, this, &AShootCharacter::PushFoodComplete);


		EnhancedInputComponent->BindAction(WheelAction, ETriggerEvent::Triggered, this, &AShootCharacter::OnMouseWheelAxis);

		// 放入背包 (T 键)：把当前手上的物品收进背包
		EnhancedInputComponent->BindAction(StoreToInventoryAction, ETriggerEvent::Triggered, this, &AShootCharacter::StoreHeldItemToInventory);

		// 从背包取出(生成到手上)：取出第一个物品
		EnhancedInputComponent->BindAction(TakeOutInventoryAction, ETriggerEvent::Triggered, this, &AShootCharacter::TakeOutFirstInventoryItem);

		// 打开/关闭背包界面 (B 键)：用 Completed(松开时触发一次)，避免 Triggered 在按住期间反复触发导致快速开合闪烁
		EnhancedInputComponent->BindAction(OpenInventoryAction, ETriggerEvent::Completed, this, &AShootCharacter::ToggleInventory);

		// 打开/关闭商店界面 (P 键)
		EnhancedInputComponent->BindAction(OpenShopAction, ETriggerEvent::Completed, this, &AShootCharacter::ToggleShop);

	}

	else
	{
		UE_LOG(LogShoot, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AShootCharacter::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void AShootCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AShootCharacter::Attack(const FInputActionValue& Value)
{
	if (CanAttack())
	{
		DoAttack();
	}
}

void AShootCharacter::Pick(const FInputActionValue& Value)
{
	// 通用对话交互：靠近带对话组件的对象时按 Pick 切换对话界面
	// 已打开 -> 再次按 E 关闭对话框；未打开 -> 打开对话框
	if (OverlappingDialogue)
	{
		if (OverlappingDialogue->IsDialogueOpen())
		{
			OverlappingDialogue->CloseDialogue();
		}
		else
		{
			OverlappingDialogue->StartDialogue(this);
		}
		return;
	}

	// 靠近植物时优先处理植物交互（浇水/驱虫/收获），不再走普通拾取逻辑
	if (OverlappingPlant)
	{
		InteractWithPlant();
		return;
	}

 	// 招揽顾客：合并到拾取键 (PickAction)，在招揽台范围内按下即触发招揽
 	if (OverlappingAttractCounter)
 	{
 		OverlappingAttractCounter->AttractCustomers();
 		return;
 	}
 	
	// 客人接单：靠近“等待接单”的客人按 E 接单，接单后才产生订单
	if (OverlappingGuest && OverlappingGuest->CanAcceptOrderByPlayer())
	{
		OverlappingGuest->AcceptOrderByPlayer();
		return;
	}

	// 靠近上菜台且手上持有物品：把成品提交上菜（菜单校验通过则成功上菜给客人）
	if (OverlappingCounter && PickingItem)
	{
		const bool bServed = OverlappingCounter->SubmitDish(PickingItem);
		if (bServed)
		{
			// 成功上菜：成品已被上菜台销毁，清空手持状态
			SetPickingItem(nullptr);
			CharacterActionState = ECharacterActionState::EAS_Unoccupied;
			CharacterState = ECharacterState::ECS_Idle;
			FBIK_Alpha = 0;
			HandIK_LPoint = nullptr;
			HandIK_RPoint = nullptr;
		}
		return;
	}

	// if (CookingMachine!=nullptr)
	// {
	// 	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::White, FString::Printf(TEXT("Bool值为: %s"), CookingMachine->IsFinish ? TEXT("True") : TEXT("False")));
	// }
 	if (MachineCookFinished())
 	{
 		//DoPutDish();//上菜碟 V1.0

 		//DoPushDishTest();//直接上菜 V2.0

 		//V3.0
 		if (IsSpawnOther())//切好菜了，生成一个对应切换的item
 		{
 			if (NeedDropDown())
 			{
 				DoDropDown();
 			}
		    else
		    {
		    	DoSpwanOther();
		    }
 			
 		}
	    else
	    {
	    	DoPushDishTest();
	    }
 		
 	}
 	// else if (CanGiveDishToGuest()) //V1.0
 	// {
 	// 	DoGiveDishToGuest();
 	// }
	else if (NeedRepairMachine())
	{
		DoRepair();
	}
	else if (CanThrowToMachine())//如果是拿着食物，则看一下是不是可以扔到锅里
	{
		DoThrow();
	}
 	else if (NeedDropDown())
 	{
 		DoDropDown();
 	}
	else if (IsSpawner())
	{
		DoSpwan();
	}
	else if (CanPick())
	{
		DoPick();
	}
}

 void AShootCharacter::PushFoodStart(const FInputActionValue& Value)
 {
 	if (NeedDropDown())
 	{
 		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("PushFoodStart"));
 		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("P键按下 - 开始绘制抛物线"));
 		bIsPKeyHeld = true;
 		CurrentAngle = 0.0f;
 	}
 }



 void AShootCharacter::PushFoodComplete(const FInputActionValue& Value)
 {
 	if (NeedDropDown())
 	{
 		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("P键松开 - 停止绘制抛物线"));
 		OnPushFoodComplete();
 		ClearPrediction();
 		bIsPKeyHeld = false;
 		CurrentAngle = 0.0f;
 	
 	}
 }

 void AShootCharacter::OnMouseWheelAxis(const FInputActionValue& Value)
 {
 	float WheelFloat = Value.Get<float>();

 	//CurrentAngle = (WheelFloat/10.0f) + CurrentAngle;

 	CurrentAngle+=WheelFloat*10.0f;
 	CurrentAngle = FMath::Clamp(CurrentAngle, MinVerticalAngle, MaxVerticalAngle);
 }


 void AShootCharacter::SetWeaponCollisionEnabled(ECollisionEnabled::Type CollisionEnabled)
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("SetWeaponCollisionEnabled"));
 	//if (EquippedWeapon == nullptr)
 	//{
 	//	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("SetWeaponCollisionEnabled nil"));
 	//}
	if (EquippedWeapon && EquippedWeapon->GetWeaponBox())
	{
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("SetWeaponCollisionEnabled: %d"),CollisionEnabled));
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("SetWeaponCollisionEnabled Come"));
		EquippedWeapon->GetWeaponBox()->SetCollisionEnabled(CollisionEnabled);
		EquippedWeapon->IgnoreActors.Empty();
	}
}

void AShootCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AShootCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// 仅保留左右旋转，禁止上下俯仰
		AddControllerYawInput(Yaw);
		if (bRotationPicth)
		{
			AddControllerPitchInput(Pitch);
		}
		
	}
}

void AShootCharacter::DoJumpStart()
{
	Jump();
}

void AShootCharacter::DoJumpEnd()
{
	StopJumping();
}



void AShootCharacter::DoAttack()
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("DoAttack"));
 	AWeapon* Weapon = GetEqiuppedWeapon();
 	if (Weapon != nullptr)
 	{
 		Weapon->AttachMeshToSocket(RetargetMesh, FName("hand_rSocket"));
 	}
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance && AttackMontage)
	{
		AnimInstance->Montage_Play(AttackMontage);
	}
	CharacterActionState = ECharacterActionState::EAS_Attacking;

}

void AShootCharacter::DoPick()
{
 	if (OverlappingItem == nullptr)
 	{
 		return;
 	}
 	IsPicking = true;
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("DoPick"));
 	if(OverlappingItem->GetItemState() != EItemState::EIS_Cooking && OverlappingItem->GetItemState() != EItemState::EIS_Equipped) //手上没有食物
	{
		// if (OverlappingItem->ItemType == EItemType::EIT_Spawn && OverlappingItem->SpawnItemActorClass)//如果是需要生成食物的类型，如番茄树，要从番茄树生成一个番茄到手上
		// {
		// 	//这一个放在他的蓝图里处理
		// 	OverlappingItem->OnSpawnActorEvent();
		// 	PlayPickMontage( FName("Pick0")); //pick1 是摘，摘完后应该切到抬
		// 	//原本在c++处理
		// 	//UWorld* World = GetWorld();
		// 	//AItemActor* Spawned = World->SpawnActor<AItemActor>(OverlappingItem->SpawnItemActorClass, GetActorLocation(), GetActorRotation());
		// 	//SetOverlappingItem(Spawned);
		// 	//SetPickingItemStatus();
		// }
		// else
		{
			//如果不是番茄树的情况
			SetPickingItemStatus();//这一个放在他的蓝图里处理
			PlayPickMontage( FName("Pick0"));
		}
	}

 	IsPicking = false;
 	//拿到手上后，把Hovering item设置为空
 	SetOverlappingItem(nullptr);
	//SetWeaponCollisionEnabled(ECollisionEnabled::NoCollision);
	
}

 void AShootCharacter::DoSpwan()
 {
 	if (OverlappingItem == nullptr)
 	{
 		return;
 	}
 	//IsPicking = true;
 	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("DoPick"));
 	if(OverlappingItem->GetItemState() != EItemState::EIS_Cooking && OverlappingItem->GetItemState() != EItemState::EIS_Equipped) //手上没有食物
 	{
 		if (OverlappingItem->ItemType == EItemType::EIT_Spawn && OverlappingItem->SpawnItemActorClass)//如果是需要生成食物的类型，如番茄树，要从番茄树生成一个番茄到手上
 		{
 			//这一个放在他的蓝图里处理
 			//OverlappingItem->OnSpawnActorEvent();
 			//PlayPickMontage( FName("Pick0")); //pick1 是摘，摘完后应该切到抬
 			
 			//原本在c++处理
 			UWorld* World = GetWorld();
 			AItemActor* Spawned = World->SpawnActor<AItemActor>(OverlappingItem->SpawnItemActorClass, GetActorLocation(), GetActorRotation());
 			SetOverlappingItem(Spawned);
 			SetPickingItemStatus();
 		}
 	}

 	//IsPicking = false;
 	//拿到手上后，把Hovering item设置为空
 	SetOverlappingItem(nullptr);
 	//SetWeaponCollisionEnabled(ECollisionEnabled::NoCollision);
 }

 void AShootCharacter::DoSpwanOther()
 {
 	CookingMachine->GetMenuFood();
 	for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 	{
 		AItemActor* CurrentActor = CookingMachine->FoodActors[i];
 		if (CurrentActor->ItemType == EItemType::EIT_SpawnOther && CurrentActor->SpawnItemActorClass)//如果是需要生成食物的类型，如牛排，要从牛排变成切好的牛排
 		{
 			UWorld* World = GetWorld();
 			AItemActor* Spawned = World->SpawnActor<AItemActor>(CurrentActor->SpawnItemActorClass, GetActorLocation(), GetActorRotation());
 			SetOverlappingItem(Spawned);
 			SetPickingItemStatus();
 		}
 		CurrentActor->Destroy();
 	}
 	CookingMachine->FoodActors.Empty();
 	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("切好菜"));
 	SetOverlappingItem(nullptr);
 }

 void AShootCharacter::DoDropDown() // 如果已经手上有食物了，要先将食物扔下
 {
 	PickingItem->DeAttachMeshToSocket();
 	//PickingItem->SetItemState(EItemState::EIS_Idle);//临时简单处理，为了可以放下食物可以马上拾取
		
 	OnReleasePickEvent();
 	PickingItem->SetItemState(EItemState::EIS_Hovering);//临时简单处理，为了可以放下食物可以马上拾取
 	SetOverlappingItem(PickingItem);//简单，先将hovering 设置成当前的手上的食物
 	SetPickingItem(nullptr);
 	CharacterActionState = ECharacterActionState::EAS_Unoccupied;
 	CharacterState = ECharacterState::ECS_Idle;

 	//设置ik
 	
 	FBIK_Alpha = 0;
 	HandIK_LPoint = nullptr;
 	HandIK_RPoint = nullptr;
 }

 void AShootCharacter::OnPushFoodComplete()
 {
 	if (PickingItem->ItemMesh->IsAnySimulatingPhysics() == false)
 	{
 		PickingItem->ItemMesh->SetSimulatePhysics(true);
 		PickingItem->ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
 	}
	FVector ForwardDirection = GetLaunchVelocity();

 	PickingItem->ItemMesh->SetPhysicsLinearVelocity(ForwardDirection);
 	

 	PickingItem->DeAttachMeshToSocket();
 	OnReleasePickEvent();
 	PickingItem->SetItemState(EItemState::EIS_Idle);
 	SetPickingItem(nullptr);
 	CharacterActionState = ECharacterActionState::EAS_Unoccupied;
 	CharacterState = ECharacterState::ECS_Idle;

 	//设置ik
 	
 	FBIK_Alpha = 0;
 	HandIK_LPoint = nullptr;
 	HandIK_RPoint = nullptr;
 }

 void AShootCharacter::SetPickingItemStatus()
{
 	AActor* PreAttachActor = OverlappingItem->GetAttachParentActor();
 	if (PreAttachActor != nullptr)
 	{
 		OverlappingItem->PreAttachActor = PreAttachActor; //FChangeAttachActorEvent
 		OverlappingItem->OnChangeAttachActorEvent.Broadcast();//广播出去，切换了
 	}
	//OverlappingItem->AttachMeshToSocket(GetMesh(), ItemSocketName);
	OverlappingItem->AttachMeshToSocket(RetargetMesh, ItemSocketName);

	OverlappingItem->SetItemState(EItemState::EIS_Equipped);//这里经常闪退
	SetPickingItem(OverlappingItem);
	OnPickEvent();
	CharacterActionState = ECharacterActionState::EAS_Picking;
	CharacterState = ECharacterState::ECS_Holding;

	//设置ik
	if (OverlappingItem->HandIK_LPoint != nullptr)
	{
		FBIK_Alpha = 1;
		HandIK_LPoint = OverlappingItem->HandIK_LPoint;
		HandIK_RPoint = OverlappingItem->HandIK_RPoint;
	}
}

void AShootCharacter::HoldItemInHand(AItemActor* Item, bool bSetAsPickingItem)
{
	if (!Item)
	{
		return;
	}

	// 从树/插槽上摘下，直接挂到角色手部插槽上
	Item->DeAttachMeshToSocket();
	Item->AttachMeshToSocket(RetargetMesh, ItemSocketName);
	Item->SetItemState(EItemState::EIS_Equipped);

	// 设为手持主物体并切换到“手持”状态（仅主物体需要，多个果实时其它只挂手上）
	if (bSetAsPickingItem)
	{
		SetPickingItem(Item);
		OnPickEvent();
		CharacterActionState = ECharacterActionState::EAS_Picking;
		CharacterState = ECharacterState::ECS_Holding;

		if (Item->HandIK_LPoint != nullptr)
		{
			FBIK_Alpha = 1;
			HandIK_LPoint = Item->HandIK_LPoint;
			HandIK_RPoint = Item->HandIK_RPoint;
		}
	}
}

// ============ 背包系统 ============
bool AShootCharacter::AddItemToInventory(AItemActor* Item)
{
	if (!Item)
	{
		return false;
	}
	if (IsInventoryFull())
	{
		UE_LOG(LogShoot, Warning, TEXT("[%s] 背包已满，无法放入 %s"),
			*GetName(), *Item->GetName());
		return false;
	}

	// 同类物品堆叠：已存在相同 ItemClass 的槽位则数量+1，不再新增槽位
	UClass* InClass = Item->GetClass();
	for (FInventorySlot& Existing : InventoryItems)
	{
		if (Existing.ItemClass == InClass)
		{
			Existing.Quantity++;
			Item->Destroy(); // 入包即删除世界中的物体
			OnInventoryChanged.Broadcast();
			UE_LOG(LogShoot, Log, TEXT("[%s] 物品 %s 已堆叠，当前数量 %d(槽位数 %d)"),
				*GetName(), *Existing.DisplayName, Existing.Quantity, InventoryItems.Num());
			return true;
		}
	}

	// 记录物体的“类型/信息”，随后销毁世界中的物体(背包只保留轻量记录)
	FInventorySlot Slot;
	Slot.ItemClass = InClass;            // 取出时按此类重新生成(保留蓝图子类)
	Slot.ItemType = Item->ItemType;
	Slot.ItemInfoRow = Item->ItemInfoRow;
	Slot.FoodIdArray = Item->FoodIdArray;
	// 显示名优先取自数据表行的 ItemDes
	if (const FItemActorInfo* InfoRow = Item->ItemInfoRow.GetRow<FItemActorInfo>(TEXT("ItemInfo")))
	{
		Slot.DisplayName = InfoRow->ItemDes;
	}
	Slot.Quantity = 1;
	InventoryItems.Add(Slot);

	Item->Destroy(); // 入包即删除世界中的物体

	OnInventoryChanged.Broadcast();
	UE_LOG(LogShoot, Log, TEXT("[%s] 物品 %s 已放入背包(已销毁世界实例)，当前数量 %d"),
		*GetName(), *Slot.DisplayName, InventoryItems.Num());
	return true;
}

bool AShootCharacter::RemoveItemFromInventory(int32 SlotIndex)
{
	if (!InventoryItems.IsValidIndex(SlotIndex))
	{
		return false;
	}
	InventoryItems.RemoveAt(SlotIndex);
	OnInventoryChanged.Broadcast();
	return true;
}

bool AShootCharacter::HasItemInInventory(int32 SlotIndex) const
{
	return InventoryItems.IsValidIndex(SlotIndex);
}

bool AShootCharacter::IsInventoryFull() const
{
	return InventoryCapacity > 0 && InventoryItems.Num() >= InventoryCapacity;
}

void AShootCharacter::StoreHeldItemToInventory()
{
	if (!PickingItem)
	{
		return;
	}
	// 先解除手持状态(从手上/插槽分离)，再记录进背包(AddItemToInventory 内部会销毁物体)
	// 注意：先保存局部指针，因为 SetPickingItem(nullptr) 会把成员 PickingItem 置空，
	// 否则下面 AddItemToInventory(PickingItem) 传进去的是 null，导致物体不会被销毁。
	AItemActor* ItemToStore = PickingItem;
	ItemToStore->DeAttachMeshToSocket();
	OnReleasePickEvent();
	SetPickingItem(nullptr);
	CharacterActionState = ECharacterActionState::EAS_Unoccupied;
	CharacterState = ECharacterState::ECS_Idle;
	FBIK_Alpha = 0;
	HandIK_LPoint = nullptr;
	HandIK_RPoint = nullptr;

	AddItemToInventory(ItemToStore); // ItemToStore 在内部被销毁，注意之后不要再访问它
}

AItemActor* AShootCharacter::TakeOutInventoryItem(int32 SlotIndex)
{
	if (!InventoryItems.IsValidIndex(SlotIndex))
	{
		return nullptr;
	}
	FInventorySlot& Slot = InventoryItems[SlotIndex];
	if (!Slot.ItemClass)
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 手上已有物体则先放下，避免冲突
	if (PickingItem)
	{
		DoDropDown();
	}

	// 按记录的类型在角色位置生成新物体
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AItemActor* Spawned = World->SpawnActor<AItemActor>(Slot.ItemClass, GetActorLocation(), GetActorRotation(), Params);
	if (!Spawned)
	{
		return nullptr;
	}

	// 回填记录的信息
	Spawned->ItemType = Slot.ItemType;
	Spawned->ItemInfoRow = Slot.ItemInfoRow;
	Spawned->FoodIdArray = Slot.FoodIdArray;

	// 装备到手上(复用拾取逻辑：挂到 ItemSocketName + 设为 PickingItem)
	Spawned->AttachMeshToSocket(RetargetMesh, ItemSocketName);
	Spawned->SetItemState(EItemState::EIS_Equipped);
	SetPickingItem(Spawned);
	OnPickEvent();
	CharacterActionState = ECharacterActionState::EAS_Picking;
	CharacterState = ECharacterState::ECS_Holding;
	if (Spawned->HandIK_LPoint != nullptr)
	{
		FBIK_Alpha = 1;
		HandIK_LPoint = Spawned->HandIK_LPoint;
		HandIK_RPoint = Spawned->HandIK_RPoint;
	}

	// 取出一件：数量-1；归零才移除该槽位(同类堆叠只减数量不删槽)
	Slot.Quantity--;
	if (Slot.Quantity <= 0)
	{
		InventoryItems.RemoveAt(SlotIndex);
	}
	OnInventoryChanged.Broadcast();
	UE_LOG(LogShoot, Log, TEXT("[%s] 从背包取出 %s，挂到手上(剩余数量 %d)"),
		*GetName(), *Slot.DisplayName, FMath::Max(Slot.Quantity, 0));
	return Spawned;
}

void AShootCharacter::TakeOutFirstInventoryItem()
{
	if (InventoryItems.Num() == 0)
	{
		return;
	}
	TakeOutInventoryItem(0);
}

void AShootCharacter::ToggleInventory()
{
	if (!InventoryWidgetClass)
	{
		UE_LOG(LogShoot, Warning, TEXT("[%s] 未配置 InventoryWidgetClass，无法打开背包"), *GetName());
		return;
	}

	// 已打开则关闭
	if (InventoryWidget && InventoryWidget->IsInViewport())
	{
		InventoryWidget->RemoveFromParent();
		return;
	}

	// 首次打开：创建并绑定刷新(只创建一次，关闭后复用)
	if (!InventoryWidget)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC)
		{
			InventoryWidget = CreateWidget<UInventoryWidget>(PC, InventoryWidgetClass);
		}
		else
		{
			InventoryWidget = CreateWidget<UInventoryWidget>(GetWorld(), InventoryWidgetClass);
		}
		if (InventoryWidget)
		{
			InventoryWidget->OwnerCharacter = this;
			// 关键：设为不可聚焦，否则 Widget 会抢占键盘焦点，导致再次按 B 时
			// 按键被 Slate 吞掉、EnhancedInput 收不到，背包无法关闭。
			InventoryWidget->SetIsFocusable(false);
			// 背包变化(增删)时自动刷新界面，只绑一次
			OnInventoryChanged.AddDynamic(InventoryWidget, &UInventoryWidget::RefreshInventory);
		}
	}

	if (InventoryWidget)
	{
		InventoryWidget->AddToViewport(10);
		InventoryWidget->RefreshInventory();
	}
}

void AShootCharacter::ToggleShop()
{
	if (!ShopWidgetClass)
	{
		UE_LOG(LogShoot, Warning, TEXT("[%s] 未配置 ShopWidgetClass，无法打开商店"), *GetName());
		return;
	}

	// 已打开则关闭
	if (ShopWidget && ShopWidget->IsInViewport())
	{
		ShopWidget->RemoveFromParent();
		return;
	}

	// 首次打开：创建并绑定经济系统(只创建一次，关闭后复用)
	if (!ShopWidget)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC)
		{
			ShopWidget = CreateWidget<UShopWidget>(PC, ShopWidgetClass);
		}
		else
		{
			ShopWidget = CreateWidget<UShopWidget>(GetWorld(), ShopWidgetClass);
		}
		if (ShopWidget)
		{
			ShopWidget->OwnerCharacter = this;
			// 设为不可聚焦，避免抢占键盘焦点导致再次按 P 失灵
			ShopWidget->SetIsFocusable(false);
			if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetGameInstance()))
			{
				ShopWidget->BindEconomy(GI->GetEconomySystem());
			}
		}
	}

	if (ShopWidget)
	{
		ShopWidget->AddToViewport(20);
		ShopWidget->RefreshShop();
	}
}



void AShootCharacter::DoThrow()
{
	if (PickingItem)
	{
		//1.播放扔的动画
		UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
		if (AnimInstance && ThrowMontage)
		{
			AnimInstance->Montage_Play(ThrowMontage);
		}
		
		
		if (CookingMachine)
		{
			//
			//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("CookingMachine"));
			// 从数据表行获取物品信息
			const FItemActorInfo* ItemRow = PickingItem->ItemInfoRow.GetRow<FItemActorInfo>(TEXT("ItemInfo"));
			if (ItemRow)
			{
				//2.
				//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("PickingItem->ItemID: %d"), ItemRow->ItemID));
			}

			if (ItemRow && CookingMachine->AddFoodMateral(ItemRow->ItemID))
			{
				PickingItem->DeAttachMeshToSocket();
				CookingMachine->FoodActors.Add(PickingItem);
				//设置到锅上的Socket,
				PickingItem->AttachMeshToSocket(CookingMachine->ItemMesh, FName("PointSocket"));
				
			//	FVector SocketWorldLocation = CookingMachine->ItemMesh->GetSocketLocation( FName("PointSocket"));
		//		PickingItem->SetActorLocation(SocketWorldLocation);
				
				PickingItem->SetItemState(EItemState::EIS_Cooking);
				OnThrowSucessEvent();	
				//设置状态在Montage蓝图设置
				CharacterActionState = ECharacterActionState::EAS_Unoccupied;
				CharacterState = ECharacterState::ECS_Idle;
				SetPickingItem(nullptr);
				//设置ik
				FBIK_Alpha = 0;
				HandIK_LPoint = nullptr;
				HandIK_RPoint = nullptr;
				//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("CookingMachine？？？"));
			}

		}

		//当机器完成后，进行推菜测试，就是cookingTime =0的时候，直接上菜
		if (MachineCookFinished())
		{
			DoPushDishTest();
		}
	}
}

 void AShootCharacter::DoRepair()
 {
	CharacterState = ECharacterState::ECS_Repairing;
	CookingMachine->Repairing = true;
	//修理的时候，人不可以动
	GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_None);
 	AWeapon* Weapon = GetEqiuppedWeapon();
 	if (Weapon != nullptr)
 	{
 		Weapon->AttachMeshToSocket(RetargetMesh, FName("hand_rSocket"));
 	}

 	//让人物朝向锅
 	// 计算看向目标的旋转
 	FVector CharacterLocation = GetActorLocation();
 	FVector TargetLocation = CookingMachine->GetActorLocation();
 	FRotator TargetRotation = (TargetLocation - CharacterLocation).Rotation();
    
 	// 设置角色旋转（只设置Yaw，保持角色直立）
 	FRotator NewRotation = FRotator(0, TargetRotation.Yaw, 0);
 	SetActorRotation(NewRotation);
 }

void AShootCharacter::DoRepairEnd()
{
	CharacterState = ECharacterState::ECS_Idle;
	CookingMachine->Repairing = false;
 	//修理完把食物删除掉
 	for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 	{
 		AItemActor* CurrentActor = CookingMachine->FoodActors[i];
 		CurrentActor->Destroy();
 	}
 	CookingMachine->FoodActors.Empty();
 	AWeapon* Weapon = GetEqiuppedWeapon();
 	if (Weapon != nullptr)
 	{
 		//Weapon->AttachMeshToSocket(GetMesh(), FName("spine_02Socket"));
 		Weapon->AttachMeshToSocket(RetargetMesh, FName("spine_02Socket"));
 		
 	}
	GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Walking);
}

 void AShootCharacter::DoPutDish()
 {
	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("菜煮好了，放碟子上"));
 	int FoodId = CookingMachine->GetMenuFood();
	auto FoodActor = Cast<UBaseGameInstance>(GetGameInstance())->foodSystem->CreateFoodActor(FoodId);
 	if (FoodActor)
 	{
 		FoodActor->AttachMeshToSocket(PickingItem->ItemMesh, FName("PointSocket"));
 		PickingItem->FoodIdArray.Add(FoodId) ;
 		PickingItem->FoodArray.Add(FoodActor);
 		for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 		{
 			AItemActor* CurrentActor = CookingMachine->FoodActors[i];
 			CurrentActor->Destroy();
 		}
 		CookingMachine->FoodActors.Empty();
 	}
 }

 void AShootCharacter::DoGiveDishToGuest()
 {
 	bool success = Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->orderSystem->PushFoods(PickingItem->FoodIdArray);
 	if (success)
 	{
 		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("成功上菜给客人"));
		OverlappingItem->OnGiveEffectEvent();//上菜的时候显示特效
		PickingItem->OnRemoveActorEvent();//移除一个碟子,放在蓝图
 		PickingItem->OnClear();
 		PickingItem->Destroy();
 		PickingItem = nullptr;
 		CharacterState = ECharacterState::ECS_Idle;
 		//移除一个碟子
 		//Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->PlateSystem->RemovePlate();
 	}
 	else
 	{
 	//	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("失败: %d"),PickingItem->FoodIdArray[0]));
 	}
 }

 void AShootCharacter::DoPushDishTest() //直接原地上菜快速测试
 {
 	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("直接原地上菜快速测试---1"));
 	int FoodId = CookingMachine->TargetMenuData.FoodID;
 	//PickingItem->FoodIdArray.Add(FoodId) ;
 	TArray<int> FoodIdArray ;
 	FoodIdArray.Add(FoodId);

 	bool success = FoodId != -1 && Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->orderSystem->PushFoods(FoodIdArray);
 	if (success)
 	{
 		CookingMachine->GetMenuFood();
 		for (int32 i = 0; i < CookingMachine->FoodActors.Num(); i++)
 		{
		    AItemActor* CurrentActor = CookingMachine->FoodActors[i];
		    CurrentActor->Destroy();
 		}
 		CookingMachine->FoodActors.Empty();
 		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("成功上菜给客人"));
 	//	OverlappingItem->OnGiveEffectEvent();//上菜的时候显示特效
 	//	PickingItem->OnRemoveActorEvent();//移除一个碟子,放在蓝图
 	//	PickingItem->OnClear();
 	//	PickingItem->Destroy();
 	//	PickingItem = nullptr;
 		CharacterState = ECharacterState::ECS_Idle;
 		//移除一个碟子
 		//Cast<UBaseGameInstance>(GetGameInstance())->curLevelBP->PlateSystem->RemovePlate();
 	}
    else
    {
    	
    	GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red
			, FString::Printf(TEXT("上菜失败%i"), FoodId));
    }
 }

 void AShootCharacter::TriggerDeadFunc(int32 RebirthTime)
 {
 	CharacterState = ECharacterState::ECS_Dead;
 	CountdownTime  = RebirthTime;
 	if (FreezeInput)
 	{
 		if (APlayerController* APC = Cast<APlayerController>(GetController()))
 		{
 			APC->SetIgnoreMoveInput(true);
 		}
 		
 	}
 	

 	// 若未通过文件夹加载，尝试直接加载路径
 	if (!BaseGrayscaleMaterial)
 	{
 		BaseGrayscaleMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/_Demo/Material/PP_Grey/M_PP_Grey"));
 	}
 	if (BaseGrayscaleMaterial&& !GrayscaleMID)
 	{
 		GrayscaleMID = UMaterialInstanceDynamic::Create(BaseGrayscaleMaterial, this);
 	}
 	if (GrayscaleMID)
 	{
 		GetFollowCamera()->PostProcessSettings.AddBlendable(GrayscaleMID, 1.0f);
 	}

 	
 	// 开始倒计时
 	// 设置计时器：每1秒执行一次UpdateCountdown，共执行3次
 	GetWorldTimerManager().SetTimer(
		 CountdownTimerHandle,
		 this,
		 &AShootCharacter::UpdateCountdown,
		 1.0f,      // 间隔1秒
		 true,      // 循环
		 0.0f       // 初始延迟0秒
	 );
 }


 void AShootCharacter::Tick(float DeltaTime)
 {
	Super::Tick(DeltaTime);

	if (CharacterState == ECharacterState::ECS_Dead)
	{
		
	}
 	
	if (IsRepairing())
	{
		if (CookingMachine->OnFire == false)
		{
			DoRepairEnd();
		}
	}

 	if (CacheAnimInstance == nullptr)
 	{
 		UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
 		UShootAnimInstance* ani = Cast<UShootAnimInstance>(AnimInstance);
 		CacheAnimInstance = ani;
 	}
	if (CacheAnimInstance != nullptr)
	{
		CacheAnimInstance->FBIK_Alpha = FBIK_Alpha;
		if (FBIK_Alpha == 1)
		{
			if (HandIK_LPoint)
			{
				auto tf = HandIK_LPoint->GetComponentTransform();
				//转坐标
				CacheAnimInstance->HandIK_L = HandIK_LPoint->GetComponentTransform();
			}
			if (HandIK_RPoint)
			{
				CacheAnimInstance->HandIK_R = HandIK_RPoint->GetComponentTransform();
			}
		}
	}

 	if (bIsPKeyHeld)
 	{
 		if (bDrawLines)
 		{
 			UpdateCharacterRotation(DeltaTime);
 		}
 		PredictAndDrawParabola();
 	}

 }

 void AShootCharacter::PredictAndDrawParabola()
 {
 	// 清除之前的绘制
 	ClearPrediction();
 	// 计算发射参数
 	FVector StartLocation = GetLaunchLocation();
 	FVector LaunchVelocity = GetLaunchVelocity();
    
 	// 设置预测参数
 	FPredictProjectilePathParams PredictParams;
 	PredictParams.StartLocation = StartLocation;
 	PredictParams.LaunchVelocity = LaunchVelocity;
 	PredictParams.ProjectileRadius = 20.0f; // 预测碰撞的半径
 	PredictParams.MaxSimTime = MaxPredictionTime;
 	PredictParams.bTraceWithCollision = true;
 	PredictParams.bTraceWithChannel = true;
 	PredictParams.TraceChannel = ECC_WorldStatic;
 	PredictParams.SimFrequency = 15.0f; // 模拟频率
 	PredictParams.OverrideGravityZ = GetWorld()->GetGravityZ() ;
 	//PredictParams.DrawDebugType = EDrawDebugTrace::None;
 	PredictParams.DrawDebugType = EDrawDebugTrace::ForDuration; // 我们自定义绘制
 	PredictParams.DrawDebugTime = 0.0f;
    
 	// 执行预测
 	FPredictProjectilePathResult PredictResult;
 	bool bSuccess = UGameplayStatics::PredictProjectilePath(
		 GetWorld(),
		 PredictParams,
		 PredictResult
	 );
    
 	if (bSuccess)
 	{
 		CurrentPathPoints.Empty();
        
 		// 存储路径点
 		for (const FPredictProjectilePathPointData& PointData : PredictResult.PathData)
 		{
 			CurrentPathPoints.Add(PointData.Location);
 		}
        
 		// 绘制抛物线
 		//DrawPredictCurve(CurrentPathPoints);
 	}
 	
 }

 void AShootCharacter::ClearPrediction()
 {
 	// 清除调试绘制（在下一帧会自动清除）
 	CurrentPathPoints.Empty();
    
 }

 void AShootCharacter::DrawPredictCurve(const TArray<FVector>& PathPoints)
 {
 	if (!GetWorld() || PathPoints.Num() < 2) return;
    
 	// 绘制点
 	for (int32 i = 0; i < PathPoints.Num(); i++)
 	{
 		DrawDebugPoint(
			 GetWorld(),
			 PathPoints[i],
			 PointSize,
			 PointColor,
			 false,
			 0.0f
		 );
        
 		// 绘制连线
 		if (bDrawLines && i > 0)
 		{
 			DrawDebugLine(
				 GetWorld(),
				 PathPoints[i-1],
				 PathPoints[i],
				 LineColor,
				 false,
				 0.0f,
				 0,
				 2.0f
			 );
 		}
 	}
    
 	// 绘制起点和终点
 	if (PathPoints.Num() > 0)
 	{
 		// 起点
 		DrawDebugSphere(
			 GetWorld(),
			 PathPoints[0],
			 15.0f,
			 12,
			 FColor::Green,
			 false,
			 0.0f
		 );
        
 		// 终点
 		DrawDebugSphere(
			 GetWorld(),
			 PathPoints.Last(),
			 15.0f,
			 12,
			 FColor::Red,
			 false,
			 0.0f
		 );
 	}
 }

 FVector AShootCharacter::GetLaunchVelocity() const
 {
	if (PickingItem == nullptr)
		return FVector::ZeroVector;
 	APlayerController* PC = Cast<APlayerController>(GetController());
	FRotator ActorRotation = GetActorRotation();
 	FRotator CameraRotation = PC->PlayerCameraManager->GetCameraRotation();

 	// 计算Pitch差
 	float PitchDiff = FMath::FindDeltaAngleDegrees(ActorRotation.Pitch, CameraRotation.Pitch); //根据镜头
 	PitchDiff = 30.0f;//CurrentAngle;//直接拿鼠标控制
 	
 	float Length = PickingItem->ImpuseIntensity ;//+ CurrentAngle * 10.0f ;
 	FRotator PitchRotator(PitchDiff, 0, 0);
 	// 3. 组合旋转：先应用角色朝向，再叠加俯仰
 	FRotator FinalRotator = ActorRotation + PitchRotator;
 	// 使用 RotateAngleAxis 绕特定轴旋转
 	FVector RotatedVector = FinalRotator.Vector();
 	
 	//FString DebugText = FString::Printf(TEXT("TargetVector = %f\n "),PitchDiff);
 	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, DebugText);
 	//DrawDebugDirectionalArrow(GetWorld(),GetLaunchLocation(),GetLaunchLocation() + RotatedVector* Length,10.0f,FColor::Red,false,-1.0f, 0,2.0f);
 	//DrawDebugDirectionalArrow(GetWorld(),GetLaunchLocation(),GetLaunchLocation() + GetActorForwardVector() * (Length+100.f),10.0f,FColor::Blue,false,-1.0f, 0,2.0f);

 	return RotatedVector * Length;
 }

 FVector AShootCharacter::GetLaunchLocation() const
 {
 	if (PickingItem)
 	{
 		return PickingItem->GetActorLocation() + FVector(0, 0, 50.f);//稍微抬高
 	}
 	return FVector::ZeroVector;
 }

 void AShootCharacter::UpdateCharacterRotation(float DeltaTime)
 {
 	APlayerController* PC = Cast<APlayerController>(GetController());
 	FRotator CameraRotation = PC->PlayerCameraManager->GetCameraRotation();

 	// 获取角色当前旋转（只考虑Yaw）
 	FRotator CharacterRotation = GetActorRotation();
    
 	// 计算角度差,// 标准化角度差到 -180 到 180 之间
 	float YawDiff = FMath::FindDeltaAngleDegrees(CharacterRotation.Yaw, CameraRotation.Yaw);
    
 	// 判断是否超过阈值
 	if (FMath::Abs(YawDiff) > RotationThreshold)
 	{
 		// 设置目标旋转（保持Pitch和Roll不变）
 		TargetTurnRotation = FRotator(CharacterRotation.Pitch, CameraRotation.Yaw, CharacterRotation.Roll);
 		bNeedRotation = true;
 	}
    
 	// 执行旋转
 	if (bNeedRotation)
 	{
 		FRotator NewRotation = FMath::RInterpTo(CharacterRotation, TargetTurnRotation, DeltaTime, RotationSpeed);
 		SetActorRotation(NewRotation);
        
 		// 检查是否旋转到位
 		float RemainingDiff = FMath::FindDeltaAngleDegrees(NewRotation.Yaw, TargetTurnRotation.Yaw);
 		if (FMath::Abs(RemainingDiff) < 1.0f)
 		{
 			bNeedRotation = false;
 		}
 	}
 }

 void AShootCharacter::UpdateCountdown()
 {
 	OnUpdateCountdownEvent();
 	// 倒计时减少
 	CountdownTime--;
 	// 检查是否结束
 	if (CountdownTime < 0)
 	{
 		// 清除计时器
 		GetWorldTimerManager().ClearTimer(CountdownTimerHandle);
        
 		// 执行倒计时结束逻辑
 		OnCountdownFinished();
 	}
 	
 }

 void AShootCharacter::OnCountdownFinished()
 {
 	OnCountdownFinishedEvent();
 	if (FreezeInput)
 	{
 		if (APlayerController* APC = Cast<APlayerController>(GetController()))
 		{
 			APC->ResetIgnoreMoveInput();
 		}
 		
 	}
 	if (GrayscaleMID)
 	{
 		GetFollowCamera()->PostProcessSettings.AddBlendable(GrayscaleMID, 0.0f);
 	}
 	// 在这里添加倒计时结束后的代码
 	// 例如：开始游戏、释放技能、播放特效等
 }
