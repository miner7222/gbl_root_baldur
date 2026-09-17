/** @file RuntimePatchBoot.c

  Runtime path for the variant EFI builds.  Reads the active-slot ABL
  partition, extracts the LinuxLoader PE from the firmware volume, applies
  the in-memory patch set and chainloads the patched image.  This replaces
  the persisted boot entry as the default boot path; the menu and its tools
  stay available on Volume Up.

  Enabled by RUNTIME_PATCH_ABL=1; the default upstream build is unchanged.
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/LinuxLoaderLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PartitionTableUpdate.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Pi/PiFirmwareFile.h>
#include <Pi/PiFirmwareVolume.h>
#include <Protocol/BlockIo.h>

#ifdef AUTO_PATCH_ABL

#include "patchs/core.h"

#define RPRINT(fmt, ...) DEBUG ((EFI_D_INFO, fmt, ##__VA_ARGS__))

/* MdeModulePkg LzmaCustomDecompressLib, declared locally like the other
 * callers in this tree. */
EFI_STATUS EFIAPI
LzmaUefiDecompressGetInfo (
  IN  CONST VOID  *Source,
  IN  UINT32       SourceSize,
  OUT UINT32      *DestinationSize,
  OUT UINT32      *ScratchSize
  );

EFI_STATUS EFIAPI
LzmaUefiDecompress (
  IN CONST VOID  *Source,
  IN UINTN        SourceSize,
  IN OUT VOID    *Destination,
  IN OUT VOID    *Scratch
  );

STATIC EFI_GUID mLzmaGuid = {
  0xEE4E5898, 0x3914, 0x4259,
  { 0x9D, 0x6E, 0xDC, 0x7B, 0xD7, 0x94, 0x03, 0xCF }
};

#pragma pack(1)
typedef struct {
  UINT32  UncompressedLength;
  UINT8   CompressionType;
} EFI_COMPRESSION_SECTION_HEADER;
#pragma pack()

#define EFI_NOT_COMPRESSED        0x00
#define EFI_STANDARD_COMPRESSION  0x01

STATIC BOOLEAN
ScanAndFindPe32 (
  IN  UINT8   *Buf,
  IN  UINTN    BufSize,
  OUT UINT8  **PeOut,
  OUT UINTN   *PeSizeOut
  );

STATIC BOOLEAN
FindPe32InSectionStream (
  IN  UINT8   *Buf,
  IN  UINTN    BufSize,
  OUT UINT8  **PeOut,
  OUT UINTN   *PeSizeOut
  );

STATIC UINTN
GetSectionSizeEx (
  IN  UINT8  *SecBase,
  OUT UINTN  *HdrSize
  )
{
  UINTN S = (UINTN)SecBase[0]
          | ((UINTN)SecBase[1] << 8)
          | ((UINTN)SecBase[2] << 16);
  if (S == 0xFFFFFF) {
    *HdrSize = 8;
    return (UINTN)(*(UINT32 *)(SecBase + 4));
  }
  *HdrSize = 4;
  return S;
}

STATIC UINTN
GetFfsSizeEx (
  IN  UINT8  *FfsBase,
  OUT UINTN  *HdrSize
  )
{
  UINT8 Attrs = FfsBase[19];
  UINTN S = (UINTN)FfsBase[20]
          | ((UINTN)FfsBase[21] << 8)
          | ((UINTN)FfsBase[22] << 16);

  if (S == 0xFFFFFF && (Attrs & 0x01)) {
    *HdrSize = 32;
    return (UINTN)(*(UINT64 *)(FfsBase + 24));
  }
  *HdrSize = 24;
  return S;
}

STATIC BOOLEAN
FindPe32InFv (
  IN  UINT8   *FvBuf,
  IN  UINTN    FvSize,
  OUT UINT8  **PeOut,
  OUT UINTN   *PeSizeOut
  )
{
  EFI_FIRMWARE_VOLUME_HEADER *FvH = (EFI_FIRMWARE_VOLUME_HEADER *)FvBuf;
  UINTN Offset;
  UINTN FvEnd;

  if (FvSize < sizeof (EFI_FIRMWARE_VOLUME_HEADER) ||
      FvH->Signature != EFI_FVH_SIGNATURE ||
      FvH->HeaderLength < sizeof (EFI_FIRMWARE_VOLUME_HEADER) ||
      FvH->HeaderLength > FvSize ||
      FvH->FvLength > (UINT64)FvSize) {
    RPRINT ("RuntimePatchBoot: invalid FV header\n");
    return FALSE;
  }

  Offset = (FvH->HeaderLength + 7) & ~(UINTN)7;
  FvEnd  = (UINTN)FvH->FvLength;

  while (Offset + 24 <= FvEnd) {
    BOOLEAN AllFF = TRUE;
    UINTN   FfsHdrSz = 0;
    UINTN   FileSize;
    UINT8   FfsType;
    UINT8  *FileData;
    UINTN   FileDataSize;
    UINT8  *Pe = NULL;
    UINTN   PeSz = 0;
    UINTN   k;

    for (k = 0; k < 24; k++) {
      if (FvBuf[Offset + k] != 0xFF) {
        AllFF = FALSE;
        break;
      }
    }
    if (AllFF) {
      Offset += 8;
      continue;
    }

    FileSize = GetFfsSizeEx (FvBuf + Offset, &FfsHdrSz);
    FfsType  = FvBuf[Offset + 18];

    if (FfsType == EFI_FV_FILETYPE_FFS_PAD) {
      if (FileSize < FfsHdrSz) break;
      Offset = (Offset + FileSize + 7) & ~(UINTN)7;
      continue;
    }

    if (FileSize < FfsHdrSz || Offset + FileSize > FvEnd) {
      RPRINT ("RuntimePatchBoot: invalid FFS size, stop\n");
      break;
    }

    FileData     = FvBuf + Offset + FfsHdrSz;
    FileDataSize = FileSize - FfsHdrSz;

    if (FindPe32InSectionStream (FileData, FileDataSize, &Pe, &PeSz)) {
      *PeOut = Pe;
      *PeSizeOut = PeSz;
      return TRUE;
    }

    Offset = (Offset + FileSize + 7) & ~(UINTN)7;
  }

  return FALSE;
}

STATIC BOOLEAN
ScanAndFindPe32 (
  IN  UINT8   *Buf,
  IN  UINTN    BufSize,
  OUT UINT8  **PeOut,
  OUT UINTN   *PeSizeOut
  )
{
  UINTN Off = 0;

  while (Off + 0x38 < BufSize) {
    UINTN Found = (UINTN)-1;
    UINTN i;
    UINT8 *FvStart;
    UINTN  FvRemain;
    EFI_FIRMWARE_VOLUME_HEADER *H;
    UINT8 *Pe = NULL;
    UINTN  PeSz = 0;

    for (i = Off; i + 4 <= BufSize; i++) {
      if (Buf[i] == '_' && Buf[i+1] == 'F' &&
          Buf[i+2] == 'V' && Buf[i+3] == 'H') {
        Found = i;
        break;
      }
    }
    if (Found == (UINTN)-1) break;

    if (Found < 0x28) {
      Off = Found + 4;
      continue;
    }

    FvStart  = Buf + Found - 0x28;
    FvRemain = BufSize - (UINTN)(FvStart - Buf);
    H = (EFI_FIRMWARE_VOLUME_HEADER *)FvStart;

    if (H->HeaderLength >= 0x48 &&
        H->HeaderLength <= 0x200 &&
        H->FvLength > (UINT64)H->HeaderLength &&
        H->FvLength <= (UINT64)FvRemain) {
      if (FindPe32InFv (FvStart, (UINTN)H->FvLength, &Pe, &PeSz)) {
        *PeOut = Pe;
        *PeSizeOut = PeSz;
        return TRUE;
      }
    }

    Off = Found + 4;
  }

  return FALSE;
}

STATIC BOOLEAN
FindPe32InSectionStream (
  IN  UINT8   *Buf,
  IN  UINTN    BufSize,
  OUT UINT8  **PeOut,
  OUT UINTN   *PeSizeOut
  )
{
  UINTN Offset = 0;

  while (Offset + 4 <= BufSize) {
    UINTN SecHdrSize = 0;
    UINTN SecSize;
    UINT8 SecType;
    UINT8 *SecData;
    UINTN  SecDataSize;

    Offset = (Offset + 3) & ~(UINTN)3;
    if (Offset + 4 > BufSize) break;

    SecSize = GetSectionSizeEx (Buf + Offset, &SecHdrSize);
    SecType = Buf[Offset + 3];

    if (SecSize < SecHdrSize || SecSize == 0 || Offset + SecSize > BufSize) {
      break;
    }

    SecData     = Buf + Offset + SecHdrSize;
    SecDataSize = SecSize - SecHdrSize;

    switch (SecType) {

    case EFI_SECTION_PE32:
    case EFI_SECTION_TE:
    {
      UINT8 *Copy = AllocatePool (SecDataSize);
      if (Copy == NULL) return FALSE;
      CopyMem (Copy, SecData, SecDataSize);
      *PeOut = Copy;
      *PeSizeOut = SecDataSize;
      return TRUE;
    }

    case EFI_SECTION_COMPRESSION:
    {
      UINT8  CompType;
      UINT8 *CompData;
      UINTN  CompLen;

      if (SecDataSize < 5) break;
      CompType = SecData[4];
      CompData = SecData + 5;
      CompLen  = SecDataSize - 5;

      if (CompType == EFI_NOT_COMPRESSED) {
        UINTN CompDataOff     = (UINTN)(CompData - Buf);
        UINTN CompDataOffAlgn = (CompDataOff + 3) & ~(UINTN)3;
        UINTN Skip            = CompDataOffAlgn - CompDataOff;
        if (CompLen > Skip) {
          UINT8 *Pe = NULL;
          UINTN  PeSz = 0;
          if (FindPe32InSectionStream (CompData + Skip, CompLen - Skip,
                                       &Pe, &PeSz)) {
            *PeOut = Pe;
            *PeSizeOut = PeSz;
            return TRUE;
          }
        }
      } else if (CompType == EFI_STANDARD_COMPRESSION) {
        UINT32 DestSize = 0;
        UINT32 ScratchSize = 0;
        UINT8 *Scratch;
        UINT8 *Dest;
        EFI_STATUS Status;
        UINT8 *Pe = NULL;
        UINTN  PeSz = 0;

        if (EFI_ERROR (LzmaUefiDecompressGetInfo (
                         CompData, (UINT32)CompLen, &DestSize, &ScratchSize)))
          break;

        Scratch = AllocatePool (ScratchSize);
        Dest    = AllocatePool (DestSize);
        if (Scratch == NULL || Dest == NULL) {
          if (Scratch != NULL) FreePool (Scratch);
          if (Dest != NULL) FreePool (Dest);
          break;
        }

        Status = LzmaUefiDecompress (CompData, (UINT32)CompLen, Dest, Scratch);
        FreePool (Scratch);
        if (EFI_ERROR (Status)) {
          FreePool (Dest);
          break;
        }

        if (FindPe32InSectionStream (Dest, DestSize, &Pe, &PeSz)) {
          FreePool (Dest);
          *PeOut = Pe;
          *PeSizeOut = PeSz;
          return TRUE;
        }
        if (ScanAndFindPe32 (Dest, DestSize, &Pe, &PeSz)) {
          FreePool (Dest);
          *PeOut = Pe;
          *PeSizeOut = PeSz;
          return TRUE;
        }
        FreePool (Dest);
      }
      break;
    }

    case EFI_SECTION_GUID_DEFINED:
    {
      EFI_GUID *Guid;
      UINT16    DataOffField;
      UINT8    *InnerData;
      UINTN     InnerSize;

      if (SecDataSize < 20) break;
      Guid = (EFI_GUID *)SecData;
      DataOffField = *(UINT16 *)(SecData + 16);
      InnerData = Buf + Offset + DataOffField;
      InnerSize = SecSize - DataOffField;

      if (InnerData + InnerSize > Buf + BufSize) break;

      if (CompareGuid (Guid, &mLzmaGuid)) {
        UINT32 DestSize = 0;
        UINT32 ScratchSize = 0;
        UINT8 *Scratch;
        UINT8 *Dest;
        EFI_STATUS Status;
        UINT8 *Pe = NULL;
        UINTN  PeSz = 0;

        if (EFI_ERROR (LzmaUefiDecompressGetInfo (
                         InnerData, (UINT32)InnerSize, &DestSize, &ScratchSize)))
          break;

        Scratch = AllocatePool (ScratchSize);
        Dest    = AllocatePool (DestSize);
        if (Scratch == NULL || Dest == NULL) {
          if (Scratch != NULL) FreePool (Scratch);
          if (Dest != NULL) FreePool (Dest);
          break;
        }

        Status = LzmaUefiDecompress (InnerData, (UINT32)InnerSize, Dest, Scratch);
        FreePool (Scratch);
        if (EFI_ERROR (Status)) {
          FreePool (Dest);
          break;
        }

        if (FindPe32InSectionStream (Dest, DestSize, &Pe, &PeSz)) {
          FreePool (Dest);
          *PeOut = Pe;
          *PeSizeOut = PeSz;
          return TRUE;
        }
        if (ScanAndFindPe32 (Dest, DestSize, &Pe, &PeSz)) {
          FreePool (Dest);
          *PeOut = Pe;
          *PeSizeOut = PeSz;
          return TRUE;
        }
        FreePool (Dest);

      } else {
        UINTN InnerStart     = Offset + DataOffField;
        UINTN InnerStartAlgn = (InnerStart + 3) & ~(UINTN)3;
        UINTN InnerEnd       = Offset + SecSize;
        if (InnerStartAlgn < InnerEnd) {
          UINT8 *Pe = NULL;
          UINTN  PeSz = 0;
          if (FindPe32InSectionStream (Buf + InnerStartAlgn,
                                       InnerEnd - InnerStartAlgn,
                                       &Pe, &PeSz)) {
            *PeOut = Pe;
            *PeSizeOut = PeSz;
            return TRUE;
          }
        }
      }
      break;
    }

    case EFI_SECTION_FIRMWARE_VOLUME_IMAGE:
    {
      UINT8 *Pe = NULL;
      UINTN  PeSz = 0;
      if (ScanAndFindPe32 (SecData, SecDataSize, &Pe, &PeSz)) {
        *PeOut = Pe;
        *PeSizeOut = PeSz;
        return TRUE;
      }
      break;
    }

    default:
      break;
    }

    Offset += SecSize;
  }

  return FALSE;
}

STATIC EFI_STATUS
GetPartitionHandle (
  IN  CHAR16                   *PartitionName,
  OUT EFI_BLOCK_IO_PROTOCOL   **PartHandle
  )
{
  EFI_STATUS        Status;
  PartiSelectFilter HandleFilter = {0};
  UINT32            BlkIOAttrib  = 0;
  HandleInfo        HandleInfoList[MAX_HANDLEINF_LST_SIZE];
  UINT32            MaxHandles   = ARRAY_SIZE (HandleInfoList);

  BlkIOAttrib |= BLK_IO_SEL_PARTITIONED_GPT;
  BlkIOAttrib |= BLK_IO_SEL_PARTITIONED_MBR;
  BlkIOAttrib |= BLK_IO_SEL_MEDIA_TYPE_NON_REMOVABLE;
  BlkIOAttrib |= BLK_IO_SEL_MATCH_PARTITION_LABEL;

  HandleFilter.PartitionLabel = PartitionName;
  HandleFilter.RootDeviceType = NULL;
  HandleFilter.VolumeName     = 0;

  Status = GetBlkIOHandles (BlkIOAttrib, &HandleFilter,
                            HandleInfoList, &MaxHandles);
  if (EFI_ERROR (Status) || MaxHandles != 1) {
    return EFI_NOT_FOUND;
  }

  *PartHandle = HandleInfoList[0].BlkIo;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
ReadEntirePartition (
  IN  CHAR16  *PartitionName,
  OUT VOID   **Buffer,
  OUT UINTN   *BufferSize
  )
{
  EFI_STATUS              Status;
  EFI_BLOCK_IO_PROTOCOL  *BlkIo = NULL;
  UINTN                   Size;
  VOID                   *Buf;

  Status = GetPartitionHandle (PartitionName, &BlkIo);
  if (EFI_ERROR (Status)) return Status;

  if (BlkIo->Media == NULL || !BlkIo->Media->MediaPresent) return EFI_NO_MEDIA;

  Size = (UINTN)BlkIo->Media->BlockSize *
         (UINTN)(BlkIo->Media->LastBlock + 1);

  Buf = AllocatePool (Size);
  if (Buf == NULL) return EFI_OUT_OF_RESOURCES;

  Status = BlkIo->ReadBlocks (BlkIo, BlkIo->Media->MediaId, 0, Size, Buf);
  if (EFI_ERROR (Status)) {
    FreePool (Buf);
    return Status;
  }

  *Buffer = Buf;
  *BufferSize = Size;
  return EFI_SUCCESS;
}

STATIC UINT8 *
FindFirmwareVolume (
  IN  UINT8  *Data,
  IN  UINTN   Size,
  OUT UINTN  *OutFvSize
  )
{
  UINTN i;

  if (Data == NULL || Size < sizeof (EFI_FIRMWARE_VOLUME_HEADER)) return NULL;

  for (i = 0; i + sizeof (EFI_FIRMWARE_VOLUME_HEADER) <= Size; i++) {
    EFI_FIRMWARE_VOLUME_HEADER *FvH = (EFI_FIRMWARE_VOLUME_HEADER *)(Data + i);
    if (FvH->Signature == EFI_FVH_SIGNATURE &&
        FvH->FvLength  >  0 &&
        FvH->FvLength  <= (UINT64)(Size - i)) {
      *OutFvSize = (UINTN)FvH->FvLength;
      return Data + i;
    }
  }
  return NULL;
}

STATIC EFI_STATUS
LoadAblPe (
  OUT CHAR8  **OutBuffer,
  OUT UINT32  *OutSize
  )
{
  CONST CHAR16 *PartNames[] = { L"abl_a", L"abl_b", L"abl" };
  UINTN         Index;

  *OutBuffer = NULL;
  *OutSize = 0;

  for (Index = 0; Index < ARRAY_SIZE (PartNames); Index++) {
    VOID   *PartBuf = NULL;
    UINTN   PartSize = 0;
    UINT8  *Fv;
    UINTN   FvSize = 0;
    UINT8  *Pe = NULL;
    UINTN   PeSize = 0;
    CHAR8  *Copy;

    if (EFI_ERROR (ReadEntirePartition ((CHAR16 *)PartNames[Index],
                                        &PartBuf, &PartSize))) {
      continue;
    }

    Fv = FindFirmwareVolume (PartBuf, PartSize, &FvSize);
    if (Fv == NULL || !FindPe32InFv (Fv, FvSize, &Pe, &PeSize)) {
      /* Some ABL images carry the PE in a nested volume that only the raw
       * scan finds. */
      if (!ScanAndFindPe32 (PartBuf, PartSize, &Pe, &PeSize)) {
        FreePool (PartBuf);
        continue;
      }
    }

    Copy = AllocatePool (PeSize);
    if (Copy == NULL) {
      FreePool (PartBuf);
      return EFI_OUT_OF_RESOURCES;
    }

    CopyMem (Copy, Pe, PeSize);
    FreePool (PartBuf);

    *OutBuffer = Copy;
    *OutSize = (UINT32)PeSize;
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

STATIC EFI_STATUS
BootImage (
  IN VOID   *Data,
  IN UINT32  Size
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  ImageHandle = NULL;

  Status = gBS->LoadImage (FALSE, gImageHandle, NULL, Data, Size, &ImageHandle);
  if (EFI_ERROR (Status)) {
    RPRINT ("RuntimePatchBoot: LoadImage failed: %r\n", Status);
    return Status;
  }

  Status = gBS->StartImage (ImageHandle, NULL, NULL);
  if (EFI_ERROR (Status)) {
    RPRINT ("RuntimePatchBoot: StartImage failed: %r\n", Status);
  }

  return Status;
}

EFI_STATUS
RuntimePatchAndBoot (VOID)
{
  EFI_STATUS  Status;
  CHAR8      *Abl = NULL;
  UINT32      AblSize = 0;

  Status = LoadAblPe (&Abl, &AblSize);
  if (EFI_ERROR (Status)) {
    RPRINT ("RuntimePatchBoot: ABL not loaded: %r\n", Status);
    return Status;
  }

  RPRINT ("RuntimePatchBoot: ABL PE %u bytes\n", AblSize);

  if (!PatchBuffer (Abl, (int32_t)AblSize)) {
    RPRINT ("RuntimePatchBoot: patch set failed\n");
    FreePool (Abl);
    return EFI_ABORTED;
  }

  Status = BootImage (Abl, AblSize);
  FreePool (Abl);
  return Status;
}

#endif /* AUTO_PATCH_ABL */
