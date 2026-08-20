// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/TerraDyneGeoTIFFReader.h"

#include "TerraDyneModule.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"

FVector2D FTerraDyneGeoTIFFRaster::PixelToCrs(double Col, double Row) const
{
	return FVector2D(A0 + Col * A1 + Row * A2, B0 + Col * B1 + Row * B2);
}

FVector2D FTerraDyneGeoTIFFRaster::CrsToPixel(double CrsX, double CrsY) const
{
	const double DX = CrsX - A0;
	const double DY = CrsY - B0;
	return FVector2D(DX * InvA1 + DY * InvA2, DX * InvB1 + DY * InvB2);
}

namespace
{
	//----------------------------------------------------------------------------------------
	// Baseline TIFF + GeoTIFF tag IDs this reader understands. Not the full spec -- only what a
	// single-band elevation raster actually needs.
	//----------------------------------------------------------------------------------------
	enum ETiffTag : uint16
	{
		Tag_ImageWidth = 256,
		Tag_ImageLength = 257,
		Tag_BitsPerSample = 258,
		Tag_Compression = 259,
		Tag_PhotometricInterpretation = 262,
		Tag_StripOffsets = 273,
		Tag_SamplesPerPixel = 277,
		Tag_RowsPerStrip = 278,
		Tag_StripByteCounts = 279,
		Tag_Predictor = 317,
		Tag_TileWidth = 322,
		Tag_TileLength = 323,
		Tag_TileOffsets = 324,
		Tag_TileByteCounts = 325,
		Tag_SampleFormat = 339,
		Tag_ModelPixelScale = 33550,
		Tag_ModelTiepoint = 33922,
		Tag_ModelTransformation = 34264,
		Tag_GeoKeyDirectory = 34735,
		Tag_GeoDoubleParams = 34736,
	};

	enum EGeoKey : uint16
	{
		GeoKey_GTModelType = 1024,
		GeoKey_GeographicType = 2048,
		GeoKey_ProjectedCSType = 3072,
	};

	enum ECompressionScheme : uint16
	{
		Compression_None = 1,
		Compression_LZW = 5,
		Compression_Deflate_Adobe = 8,
		Compression_Deflate_Legacy = 32946,
	};

	enum ESampleFormat : uint16
	{
		SampleFormat_UnsignedInt = 1,
		SampleFormat_SignedInt = 2,
		SampleFormat_IEEEFloat = 3,
	};

	//----------------------------------------------------------------------------------------
	// Endian-aware byte-buffer reader. TIFF files declare their own byte order in the header, so
	// every multi-byte read has to go through this rather than assuming native/platform order.
	//----------------------------------------------------------------------------------------
	class FByteReader
	{
	public:
		FByteReader(const uint8* InData, int64 InSize, bool bInLittleEndian)
			: Data(InData), Size(InSize), bLittleEndian(bInLittleEndian)
		{
		}

		bool CanRead(int64 Offset, int64 NumBytes) const
		{
			return Offset >= 0 && NumBytes >= 0 && Offset + NumBytes <= Size;
		}

		uint16 ReadU16(int64 Offset) const
		{
			if (!CanRead(Offset, 2)) return 0;
			const uint8 B0 = Data[Offset], B1 = Data[Offset + 1];
			return bLittleEndian ? (uint16(B1) << 8 | B0) : (uint16(B0) << 8 | B1);
		}

		uint32 ReadU32(int64 Offset) const
		{
			if (!CanRead(Offset, 4)) return 0;
			const uint8 B0 = Data[Offset], B1 = Data[Offset + 1], B2 = Data[Offset + 2], B3 = Data[Offset + 3];
			return bLittleEndian
				? (uint32(B3) << 24 | uint32(B2) << 16 | uint32(B1) << 8 | B0)
				: (uint32(B0) << 24 | uint32(B1) << 16 | uint32(B2) << 8 | B3);
		}

		double ReadDouble(int64 Offset) const
		{
			if (!CanRead(Offset, 8)) return 0.0;
			uint64 Bits = 0;
			for (int32 i = 0; i < 8; ++i)
			{
				const uint8 Byte = Data[Offset + i];
				const int32 ShiftIndex = bLittleEndian ? i : (7 - i);
				Bits |= (uint64(Byte) << (ShiftIndex * 8));
			}
			double Result;
			FMemory::Memcpy(&Result, &Bits, sizeof(double));
			return Result;
		}

		float ReadFloat(int64 Offset) const
		{
			if (!CanRead(Offset, 4)) return 0.0f;
			const uint32 Bits = ReadU32(Offset);
			float Result;
			FMemory::Memcpy(&Result, &Bits, sizeof(float));
			return Result;
		}

		const uint8* Data;
		int64 Size;
		bool bLittleEndian;
	};

	/** Byte size of one value of a TIFF field type. 0 for types this reader never needs to size. */
	int32 TiffTypeSize(uint16 FieldType)
	{
		switch (FieldType)
		{
		case 1: case 2: case 6: case 7: return 1;  // BYTE, ASCII, SBYTE, UNDEFINED
		case 3: case 8: return 2;                  // SHORT, SSHORT
		case 4: case 9: return 4;                  // LONG, SLONG
		case 5: case 10: return 8;                 // RATIONAL, SRATIONAL (2x LONG)
		case 11: return 4;                         // FLOAT
		case 12: return 8;                         // DOUBLE
		default: return 0;
		}
	}

	/** One parsed IFD entry, with its value(s) resolved to plain arrays regardless of source type. */
	struct FIfdEntry
	{
		uint16 Tag = 0;
		uint16 FieldType = 0;
		uint32 Count = 0;
		int64 ValueOrOffset = 0; // raw 4-byte field, interpretation depends on whether data fits inline

		TArray<uint32> AsUInts(const FByteReader& Reader) const
		{
			TArray<uint32> Out;
			Out.Reserve(Count);
			const int32 ElemSize = TiffTypeSize(FieldType);
			if (ElemSize == 0) return Out;

			const bool bInline = (int64)ElemSize * Count <= 4;
			const int64 Base = bInline ? -1 : ValueOrOffset; // resolved per-element below

			for (uint32 i = 0; i < Count; ++i)
			{
				uint32 Value = 0;
				if (bInline)
				{
					// Inline values are packed into the 4-byte field itself, in file byte order,
					// left-justified -- only SHORT/BYTE realistically end up here for our tags.
					if (FieldType == 3) // SHORT
					{
						Value = (i == 0) ? (Reader.bLittleEndian ? (ValueOrOffset & 0xFFFF) : ((ValueOrOffset >> 16) & 0xFFFF))
							: (Reader.bLittleEndian ? ((ValueOrOffset >> 16) & 0xFFFF) : (ValueOrOffset & 0xFFFF));
					}
					else if (FieldType == 1 || FieldType == 2 || FieldType == 6 || FieldType == 7) // BYTE-ish
					{
						const int32 Shift = Reader.bLittleEndian ? (i * 8) : (24 - i * 8);
						Value = (ValueOrOffset >> Shift) & 0xFF;
					}
					else if (FieldType == 4) // LONG, single value fits inline
					{
						Value = (uint32)ValueOrOffset;
					}
				}
				else
				{
					const int64 Offset = Base + (int64)i * ElemSize;
					if (FieldType == 3) Value = Reader.ReadU16(Offset);
					else if (FieldType == 4) Value = Reader.ReadU32(Offset);
					else if (FieldType == 1 || FieldType == 2 || FieldType == 6 || FieldType == 7)
						Value = Reader.CanRead(Offset, 1) ? Reader.Data[Offset] : 0;
				}
				Out.Add(Value);
			}
			return Out;
		}

		TArray<double> AsDoubles(const FByteReader& Reader) const
		{
			TArray<double> Out;
			Out.Reserve(Count);
			if (FieldType == 12) // DOUBLE
			{
				for (uint32 i = 0; i < Count; ++i)
					Out.Add(Reader.ReadDouble(ValueOrOffset + (int64)i * 8));
			}
			return Out;
		}

		uint32 AsSingleUInt(const FByteReader& Reader) const
		{
			TArray<uint32> Vals = AsUInts(Reader);
			return Vals.Num() > 0 ? Vals[0] : 0;
		}
	};

	//----------------------------------------------------------------------------------------
	// TIFF-variant LZW decompression.
	//
	// Differs from the "textbook"/GIF LZW this superficially resembles in one deliberate way:
	// TIFF uses early change -- the code width grows one code sooner than the naive power-of-two
	// boundary (at table size 511/1023/2047 rather than 512/1024/2048). Get this wrong and the
	// decoded stream silently desyncs into garbage a few hundred codes in rather than failing
	// outright, which is exactly the kind of bug that is easy to ship unnoticed -- so the table
	// size thresholds below are the one thing in this function that must match the TIFF6 spec
	// exactly, not "close enough".
	//----------------------------------------------------------------------------------------
	bool DecompressLZW(const uint8* Src, int64 SrcSize, TArray<uint8>& OutDst, int64 ExpectedDstSize)
	{
		constexpr uint16 ClearCode = 256;
		constexpr uint16 EndOfInfoCode = 257;
		constexpr uint16 FirstDynamicCode = 258;
		constexpr int32 MaxCodeBits = 12;
		constexpr int32 MaxTableSize = 1 << MaxCodeBits;

		OutDst.Reset();
		OutDst.Reserve((int32)(ExpectedDstSize > 0 ? ExpectedDstSize : SrcSize * 3));

		TArray<TArray<uint8>> Table;
		Table.SetNum(MaxTableSize);
		auto ResetTable = [&Table]()
		{
			for (int32 i = 0; i < 256; ++i)
			{
				Table[i].Reset(1);
				Table[i].Add((uint8)i);
			}
			for (int32 i = 256; i < MaxTableSize; ++i)
			{
				Table[i].Reset();
			}
		};
		ResetTable();

		int32 NextCode = FirstDynamicCode;
		int32 CodeBits = 9;
		int64 BitPos = 0;
		const int64 TotalBits = SrcSize * 8;

		auto ReadCode = [&]() -> int32
		{
			if (BitPos + CodeBits > TotalBits) return -1;
			uint32 Value = 0;
			for (int32 i = 0; i < CodeBits; ++i)
			{
				const int64 Bit = BitPos + i;
				const uint8 Byte = Src[Bit / 8];
				const int32 BitInByte = 7 - (Bit % 8); // TIFF LZW packs bits MSB-first
				const uint32 BitVal = (Byte >> BitInByte) & 1;
				Value = (Value << 1) | BitVal;
			}
			BitPos += CodeBits;
			return (int32)Value;
		};

		TArray<uint8> Previous;
		int32 Code = ReadCode();
		if (Code != ClearCode)
		{
			return false; // a well-formed stream always opens with a clear code
		}

		for (;;)
		{
			Code = ReadCode();
			if (Code < 0 || Code == EndOfInfoCode) break;

			if (Code == ClearCode)
			{
				ResetTable();
				NextCode = FirstDynamicCode;
				CodeBits = 9;
				Previous.Reset();
				continue;
			}

			TArray<uint8> Entry;
			if (Code < NextCode && Table[Code].Num() > 0)
			{
				Entry = Table[Code];
			}
			else if (Code == NextCode && Previous.Num() > 0)
			{
				// The one legal "not yet in the table" case: cScSc.
				Entry = Previous;
				Entry.Add(Previous[0]);
			}
			else
			{
				return false; // corrupt or unsupported stream
			}

			OutDst.Append(Entry);

			if (Previous.Num() > 0 && NextCode < MaxTableSize)
			{
				TArray<uint8> NewEntry = Previous;
				NewEntry.Add(Entry[0]);
				Table[NextCode] = MoveTemp(NewEntry);
				NextCode++;

				// Early change: widen one code sooner than table size would otherwise demand.
				if (NextCode == 511) CodeBits = 10;
				else if (NextCode == 1023) CodeBits = 11;
				else if (NextCode == 2047) CodeBits = 12;
			}

			Previous = MoveTemp(Entry);

			if (ExpectedDstSize > 0 && OutDst.Num() >= ExpectedDstSize) break;
		}

		return true;
	}

	/** Undoes TIFF Predictor 2 (horizontal differencing) in place, per row, per sample channel. */
	void UndoHorizontalPredictor(TArray<uint8>& Data, int32 Width, int32 Height, int32 BytesPerSample, int32 SamplesPerPixel, bool bIsFloat)
	{
		if (bIsFloat)
		{
			// Floating-point predictor is a distinct, byte-shuffled scheme (Predictor 3) -- this
			// function only undoes the integer horizontal predictor (2), which is the one actually
			// reachable here since the reader rejects Predictor 3 outright (see LoadFromFile).
			return;
		}

		const int32 RowStride = Width * SamplesPerPixel * BytesPerSample;
		for (int32 Row = 0; Row < Height; ++Row)
		{
			uint8* RowPtr = Data.GetData() + (int64)Row * RowStride;
			for (int32 Col = 1; Col < Width; ++Col)
			{
				for (int32 Sample = 0; Sample < SamplesPerPixel; ++Sample)
				{
					const int32 Offset = (Col * SamplesPerPixel + Sample) * BytesPerSample;
					const int32 PrevOffset = ((Col - 1) * SamplesPerPixel + Sample) * BytesPerSample;
					if (BytesPerSample == 1)
					{
						RowPtr[Offset] = (uint8)(RowPtr[Offset] + RowPtr[PrevOffset]);
					}
					else if (BytesPerSample == 2)
					{
						uint16 Cur, Prev;
						FMemory::Memcpy(&Cur, RowPtr + Offset, 2);
						FMemory::Memcpy(&Prev, RowPtr + PrevOffset, 2);
						Cur = (uint16)(Cur + Prev);
						FMemory::Memcpy(RowPtr + Offset, &Cur, 2);
					}
					else if (BytesPerSample == 4)
					{
						uint32 Cur, Prev;
						FMemory::Memcpy(&Cur, RowPtr + Offset, 4);
						FMemory::Memcpy(&Prev, RowPtr + PrevOffset, 4);
						Cur = Cur + Prev;
						FMemory::Memcpy(RowPtr + Offset, &Cur, 4);
					}
				}
			}
		}
	}

	/** Reads one sample at (Col,Row) out of a decoded, predictor-undone raw sample buffer. */
	float ReadSample(const TArray<uint8>& Data, int32 Width, int32 Col, int32 Row, int32 BytesPerSample, uint16 SampleFormat, bool bLittleEndian)
	{
		const int64 Offset = ((int64)Row * Width + Col) * BytesPerSample;
		if (Offset < 0 || Offset + BytesPerSample > Data.Num()) return 0.0f;

		auto ReadRaw = [&](int32 NumBytes) -> uint64
		{
			uint64 Value = 0;
			for (int32 i = 0; i < NumBytes; ++i)
			{
				const uint8 Byte = Data[Offset + i];
				const int32 ShiftIndex = bLittleEndian ? i : (NumBytes - 1 - i);
				Value |= (uint64(Byte) << (ShiftIndex * 8));
			}
			return Value;
		};

		if (SampleFormat == SampleFormat_IEEEFloat)
		{
			if (BytesPerSample == 4)
			{
				const uint32 Bits = (uint32)ReadRaw(4);
				float Result;
				FMemory::Memcpy(&Result, &Bits, 4);
				return Result;
			}
			if (BytesPerSample == 8)
			{
				const uint64 Bits = ReadRaw(8);
				double Result;
				FMemory::Memcpy(&Result, &Bits, 8);
				return (float)Result;
			}
			return 0.0f;
		}

		const uint64 Raw = ReadRaw(BytesPerSample);
		if (SampleFormat == SampleFormat_SignedInt)
		{
			if (BytesPerSample == 2) return (float)(int16)Raw;
			if (BytesPerSample == 4) return (float)(int32)Raw;
			if (BytesPerSample == 1) return (float)(int8)Raw;
		}
		// Unsigned (default assumption for BitsPerSample with no SampleFormat tag present).
		return (float)Raw;
	}
}

bool FTerraDyneGeoTIFFReader::LoadFromFile(const FString& FilePath, FTerraDyneGeoTIFFRaster& OutRaster, FString& OutError)
{
	OutRaster = FTerraDyneGeoTIFFRaster();
	OutError.Reset();

	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath))
	{
		OutError = FString::Printf(TEXT("Could not open file: %s"), *FilePath);
		return false;
	}
	if (FileBytes.Num() < 8)
	{
		OutError = TEXT("File is too small to be a valid TIFF.");
		return false;
	}

	const bool bLittleEndian = (FileBytes[0] == 'I' && FileBytes[1] == 'I');
	const bool bBigEndian = (FileBytes[0] == 'M' && FileBytes[1] == 'M');
	if (!bLittleEndian && !bBigEndian)
	{
		OutError = TEXT("Not a TIFF file (bad byte-order marker).");
		return false;
	}

	FByteReader Reader(FileBytes.GetData(), FileBytes.Num(), bLittleEndian);
	if (Reader.ReadU16(2) != 42)
	{
		OutError = TEXT("Not a TIFF file (bad magic number).");
		return false;
	}

	const int64 FirstIfdOffset = Reader.ReadU32(4);
	if (!Reader.CanRead(FirstIfdOffset, 2))
	{
		OutError = TEXT("TIFF file has no readable image directory.");
		return false;
	}

	// Only the first IFD is read -- a multi-page TIFF's later pages are out of scope; a DEM export
	// is expected to be single-page.
	const uint16 EntryCount = Reader.ReadU16(FirstIfdOffset);
	TMap<uint16, FIfdEntry> Entries;
	for (uint16 i = 0; i < EntryCount; ++i)
	{
		const int64 EntryOffset = FirstIfdOffset + 2 + (int64)i * 12;
		FIfdEntry Entry;
		Entry.Tag = Reader.ReadU16(EntryOffset);
		Entry.FieldType = Reader.ReadU16(EntryOffset + 2);
		Entry.Count = Reader.ReadU32(EntryOffset + 4);

		// Raw 4-byte field, verbatim: AsUInts/AsDoubles interpret it as either the inline value or a
		// file offset depending on whether ElemSize*Count fits in 4 bytes, so there's nothing to
		// branch on here -- the bytes read are identical either way.
		Entry.ValueOrOffset = Reader.ReadU32(EntryOffset + 8);
		Entries.Add(Entry.Tag, Entry);
	}

	const FIfdEntry* WidthEntry = Entries.Find(Tag_ImageWidth);
	const FIfdEntry* HeightEntry = Entries.Find(Tag_ImageLength);
	if (!WidthEntry || !HeightEntry)
	{
		OutError = TEXT("TIFF is missing ImageWidth/ImageLength.");
		return false;
	}
	const int32 Width = (int32)WidthEntry->AsSingleUInt(Reader);
	const int32 Height = (int32)HeightEntry->AsSingleUInt(Reader);
	if (Width <= 0 || Height <= 0)
	{
		OutError = TEXT("TIFF reports a zero or negative image size.");
		return false;
	}

	const int32 SamplesPerPixel = Entries.Contains(Tag_SamplesPerPixel) ? (int32)Entries[Tag_SamplesPerPixel].AsSingleUInt(Reader) : 1;
	if (SamplesPerPixel != 1)
	{
		OutError = FString::Printf(TEXT("Only single-band elevation GeoTIFFs are supported; this file has %d bands."), SamplesPerPixel);
		return false;
	}

	const int32 BitsPerSample = Entries.Contains(Tag_BitsPerSample) ? (int32)Entries[Tag_BitsPerSample].AsSingleUInt(Reader) : 8;
	const int32 BytesPerSample = BitsPerSample / 8;
	if (BytesPerSample != 1 && BytesPerSample != 2 && BytesPerSample != 4)
	{
		OutError = FString::Printf(TEXT("Unsupported BitsPerSample (%d); expected 8, 16, or 32."), BitsPerSample);
		return false;
	}

	const uint16 SampleFormat = Entries.Contains(Tag_SampleFormat) ? (uint16)Entries[Tag_SampleFormat].AsSingleUInt(Reader) : SampleFormat_UnsignedInt;
	if (SampleFormat == SampleFormat_IEEEFloat && BytesPerSample != 4 && BytesPerSample != 8)
	{
		OutError = TEXT("Unsupported floating-point sample size (expected 32 or 64 bit).");
		return false;
	}

	const uint16 Compression = Entries.Contains(Tag_Compression) ? (uint16)Entries[Tag_Compression].AsSingleUInt(Reader) : Compression_None;
	if (Compression != Compression_None && Compression != Compression_LZW &&
		Compression != Compression_Deflate_Adobe && Compression != Compression_Deflate_Legacy)
	{
		OutError = FString::Printf(TEXT("Unsupported TIFF compression scheme (%d); expected none, LZW, or Deflate."), Compression);
		return false;
	}

	const uint16 Predictor = Entries.Contains(Tag_Predictor) ? (uint16)Entries[Tag_Predictor].AsSingleUInt(Reader) : 1;
	if (Predictor != 1 && Predictor != 2)
	{
		OutError = FString::Printf(TEXT("Unsupported TIFF predictor (%d); expected none or horizontal differencing."), Predictor);
		return false;
	}

	// ---- Assemble the raw (post-decompression, post-predictor) sample buffer ----
	//
	// Handles both classic strip layout and the tiled layout modern GeoTIFF/COG exporters commonly
	// use. Either way the result is one contiguous Width*Height*BytesPerSample buffer in row-major
	// order, so everything downstream is layout-agnostic.
	TArray<uint8> RawSamples;
	RawSamples.SetNumZeroed((int32)((int64)Width * Height * BytesPerSample));

	auto DecompressChunk = [&](const uint8* Src, int64 SrcSize, int64 ExpectedSize, TArray<uint8>& OutChunk) -> bool
	{
		if (Compression == Compression_None)
		{
			OutChunk.SetNumUninitialized((int32)SrcSize);
			FMemory::Memcpy(OutChunk.GetData(), Src, SrcSize);
			return true;
		}
		if (Compression == Compression_LZW)
		{
			return DecompressLZW(Src, SrcSize, OutChunk, ExpectedSize);
		}
		// Adobe/legacy Deflate: raw TIFF strips/tiles store a standard zlib stream.
		OutChunk.SetNumUninitialized((int32)ExpectedSize);
		return FCompression::UncompressMemory(NAME_Zlib, OutChunk.GetData(), (int32)ExpectedSize, Src, (int32)SrcSize);
	};

	const bool bTiled = Entries.Contains(Tag_TileWidth);
	if (bTiled)
	{
		const int32 TileWidth = (int32)Entries[Tag_TileWidth].AsSingleUInt(Reader);
		const int32 TileHeight = (int32)Entries[Tag_TileLength].AsSingleUInt(Reader);
		const FIfdEntry* OffsetsEntry = Entries.Find(Tag_TileOffsets);
		const FIfdEntry* ByteCountsEntry = Entries.Find(Tag_TileByteCounts);
		if (TileWidth <= 0 || TileHeight <= 0 || !OffsetsEntry || !ByteCountsEntry)
		{
			OutError = TEXT("TIFF is tiled but missing tile geometry/offset tags.");
			return false;
		}
		const TArray<uint32> Offsets = OffsetsEntry->AsUInts(Reader);
		const TArray<uint32> ByteCounts = ByteCountsEntry->AsUInts(Reader);

		const int32 TilesAcross = FMath::DivideAndRoundUp(Width, TileWidth);
		const int32 TilesDown = FMath::DivideAndRoundUp(Height, TileHeight);
		const int64 TileSampleCount = (int64)TileWidth * TileHeight;

		for (int32 TileRow = 0; TileRow < TilesDown; ++TileRow)
		{
			for (int32 TileCol = 0; TileCol < TilesAcross; ++TileCol)
			{
				const int32 TileIndex = TileRow * TilesAcross + TileCol;
				if (!Offsets.IsValidIndex(TileIndex) || !ByteCounts.IsValidIndex(TileIndex)) continue;
				if (!Reader.CanRead(Offsets[TileIndex], ByteCounts[TileIndex])) continue;

				TArray<uint8> Decompressed;
				if (!DecompressChunk(Reader.Data + Offsets[TileIndex], ByteCounts[TileIndex],
					TileSampleCount * BytesPerSample, Decompressed))
				{
					OutError = TEXT("Failed to decompress a TIFF tile.");
					return false;
				}
				if ((int64)Decompressed.Num() < TileSampleCount * BytesPerSample)
				{
					// A short/malformed tile: UndoHorizontalPredictor below would read past the end
					// of Decompressed if allowed to proceed on the tile's full declared dimensions.
					OutError = TEXT("Decompressed TIFF tile is smaller than its declared dimensions.");
					return false;
				}
				if (Predictor == 2)
				{
					UndoHorizontalPredictor(Decompressed, TileWidth, TileHeight, BytesPerSample, 1, SampleFormat == SampleFormat_IEEEFloat);
				}

				// Copy this tile's rows into the full-image buffer, clipping edge tiles that
				// overhang the image (TIFF tiles are always full-size; the last row/column of
				// tiles is padded).
				const int32 DstBaseCol = TileCol * TileWidth;
				const int32 DstBaseRow = TileRow * TileHeight;
				const int32 CopyWidth = FMath::Min(TileWidth, Width - DstBaseCol);
				const int32 CopyHeight = FMath::Min(TileHeight, Height - DstBaseRow);
				for (int32 Row = 0; Row < CopyHeight; ++Row)
				{
					const uint8* SrcRow = Decompressed.GetData() + (int64)Row * TileWidth * BytesPerSample;
					uint8* DstRow = RawSamples.GetData() + ((int64)(DstBaseRow + Row) * Width + DstBaseCol) * BytesPerSample;
					FMemory::Memcpy(DstRow, SrcRow, (int64)CopyWidth * BytesPerSample);
				}
			}
		}
	}
	else
	{
		const FIfdEntry* OffsetsEntry = Entries.Find(Tag_StripOffsets);
		const FIfdEntry* ByteCountsEntry = Entries.Find(Tag_StripByteCounts);
		const int32 RowsPerStrip = Entries.Contains(Tag_RowsPerStrip) ? (int32)Entries[Tag_RowsPerStrip].AsSingleUInt(Reader) : Height;
		if (!OffsetsEntry || !ByteCountsEntry || RowsPerStrip <= 0)
		{
			OutError = TEXT("TIFF is missing strip geometry/offset tags.");
			return false;
		}
		const TArray<uint32> Offsets = OffsetsEntry->AsUInts(Reader);
		const TArray<uint32> ByteCounts = ByteCountsEntry->AsUInts(Reader);

		int32 RowsWritten = 0;
		for (int32 StripIndex = 0; StripIndex < Offsets.Num() && RowsWritten < Height; ++StripIndex)
		{
			if (!ByteCounts.IsValidIndex(StripIndex)) break;
			if (!Reader.CanRead(Offsets[StripIndex], ByteCounts[StripIndex])) continue;

			const int32 RowsInThisStrip = FMath::Min(RowsPerStrip, Height - RowsWritten);
			const int64 ExpectedSize = (int64)RowsInThisStrip * Width * BytesPerSample;

			TArray<uint8> Decompressed;
			if (!DecompressChunk(Reader.Data + Offsets[StripIndex], ByteCounts[StripIndex], ExpectedSize, Decompressed))
			{
				OutError = TEXT("Failed to decompress a TIFF strip.");
				return false;
			}
			if ((int64)Decompressed.Num() < ExpectedSize)
			{
				// Same short/malformed-input guard as the tiled path above.
				OutError = TEXT("Decompressed TIFF strip is smaller than its declared dimensions.");
				return false;
			}
			if (Predictor == 2)
			{
				UndoHorizontalPredictor(Decompressed, Width, RowsInThisStrip, BytesPerSample, 1, SampleFormat == SampleFormat_IEEEFloat);
			}

			uint8* Dst = RawSamples.GetData() + (int64)RowsWritten * Width * BytesPerSample;
			const int64 CopySize = FMath::Min((int64)Decompressed.Num(), ExpectedSize);
			FMemory::Memcpy(Dst, Decompressed.GetData(), CopySize);
			RowsWritten += RowsInThisStrip;
		}
	}

	// ---- Convert raw samples to metres, tracking min/max as we go ----
	OutRaster.Width = Width;
	OutRaster.Height = Height;
	OutRaster.ElevationMeters.SetNumUninitialized((int32)((int64)Width * Height));
	double MinZ = TNumericLimits<double>::Max();
	double MaxZ = TNumericLimits<double>::Lowest();
	for (int32 Row = 0; Row < Height; ++Row)
	{
		for (int32 Col = 0; Col < Width; ++Col)
		{
			const float Z = ReadSample(RawSamples, Width, Col, Row, BytesPerSample, SampleFormat, bLittleEndian);
			OutRaster.ElevationMeters[(int64)Row * Width + Col] = Z;
			MinZ = FMath::Min(MinZ, (double)Z);
			MaxZ = FMath::Max(MaxZ, (double)Z);
		}
	}
	OutRaster.ElevationMinMeters = MinZ;
	OutRaster.ElevationMaxMeters = MaxZ;

	// ---- Affine transform ----
	if (const FIfdEntry* XformEntry = Entries.Find(Tag_ModelTransformation))
	{
		const TArray<double> M = XformEntry->AsDoubles(Reader);
		if (M.Num() >= 16)
		{
			// Row-major 4x4: [ A1 A2 A3 A0 ; B1 B2 B3 B0 ; ... ] -- only the 2D terms matter here.
			OutRaster.A1 = M[0]; OutRaster.A2 = M[1]; OutRaster.A0 = M[3];
			OutRaster.B1 = M[4]; OutRaster.B2 = M[5]; OutRaster.B0 = M[7];
		}
	}
	else
	{
		const FIfdEntry* ScaleEntry = Entries.Find(Tag_ModelPixelScale);
		const FIfdEntry* TiepointEntry = Entries.Find(Tag_ModelTiepoint);
		if (!ScaleEntry || !TiepointEntry)
		{
			OutError = TEXT("GeoTIFF has no ModelPixelScale/ModelTiepoint or ModelTransformation tag -- cannot georeference it.");
			return false;
		}
		const TArray<double> Scale = ScaleEntry->AsDoubles(Reader);
		const TArray<double> Tie = TiepointEntry->AsDoubles(Reader);
		if (Scale.Num() < 2 || Tie.Num() < 6)
		{
			OutError = TEXT("GeoTIFF's georeferencing tags are malformed.");
			return false;
		}
		// Tiepoint: raster (I,J,K) -> CRS (X,Y,Z). PixelScale gives the CRS-units-per-pixel step;
		// Y decreases as row increases (image space is top-down, most CRSs are north-up), hence -ScaleY.
		const double ScaleX = Scale[0];
		const double ScaleY = Scale[1];
		const double TieI = Tie[0], TieJ = Tie[1], TieX = Tie[3], TieY = Tie[4];
		OutRaster.A1 = ScaleX; OutRaster.A2 = 0.0; OutRaster.A0 = TieX - TieI * ScaleX;
		OutRaster.B1 = 0.0; OutRaster.B2 = -ScaleY; OutRaster.B0 = TieY + TieJ * ScaleY;
	}

	const double Det = OutRaster.A1 * OutRaster.B2 - OutRaster.A2 * OutRaster.B1;
	if (FMath::IsNearlyZero(Det))
	{
		OutError = TEXT("GeoTIFF's affine transform is singular (degenerate pixel scale).");
		return false;
	}
	OutRaster.InvA1 = OutRaster.B2 / Det;
	OutRaster.InvA2 = -OutRaster.A2 / Det;
	OutRaster.InvB1 = -OutRaster.B1 / Det;
	OutRaster.InvB2 = OutRaster.A1 / Det;

	// ---- CRS classification via the GeoKey directory ----
	const FIfdEntry* GeoKeysEntry = Entries.Find(Tag_GeoKeyDirectory);
	if (!GeoKeysEntry)
	{
		OutError = TEXT("GeoTIFF has no GeoKeyDirectoryTag -- cannot determine its coordinate system.");
		return false;
	}
	const TArray<uint32> RawKeys = GeoKeysEntry->AsUInts(Reader);
	if (RawKeys.Num() < 4)
	{
		OutError = TEXT("GeoTIFF's GeoKeyDirectoryTag is malformed.");
		return false;
	}
	const uint32 NumKeys = RawKeys[3];
	uint32 ModelType = 0, GeographicType = 0, ProjectedCSType = 0;
	for (uint32 k = 0; k < NumKeys; ++k)
	{
		const int32 Base = 4 + k * 4;
		if (Base + 3 >= RawKeys.Num()) break;
		const uint32 KeyID = RawKeys[Base];
		const uint32 TagLocation = RawKeys[Base + 1];
		const uint32 ValueOrOffset = RawKeys[Base + 3];
		if (TagLocation != 0) continue; // key value stored in another tag (GeoDoubleParams/ASCII) -- not needed for the keys we read
		if (KeyID == GeoKey_GTModelType) ModelType = ValueOrOffset;
		else if (KeyID == GeoKey_GeographicType) GeographicType = ValueOrOffset;
		else if (KeyID == GeoKey_ProjectedCSType) ProjectedCSType = ValueOrOffset;
	}

	if (ModelType == 2) // Geographic
	{
		if (GeographicType != 4326 && GeographicType != 0 && GeographicType != 32767)
		{
			OutError = FString::Printf(TEXT("Unsupported geographic datum (EPSG:%d) -- only WGS84 (EPSG:4326) is supported."), GeographicType);
			return false;
		}
		OutRaster.bIsGeographicWGS84 = true;
	}
	else if (ModelType == 1) // Projected
	{
		// Standard WGS84 UTM EPSG numbering: 326xx = north zone xx, 327xx = south zone xx.
		if (ProjectedCSType >= 32601 && ProjectedCSType <= 32660)
		{
			OutRaster.bIsUTM = true;
			OutRaster.UTMZone = ProjectedCSType - 32600;
			OutRaster.bUTMNorth = true;
		}
		else if (ProjectedCSType >= 32701 && ProjectedCSType <= 32760)
		{
			OutRaster.bIsUTM = true;
			OutRaster.UTMZone = ProjectedCSType - 32700;
			OutRaster.bUTMNorth = false;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported projected coordinate system (EPSG:%d) -- only standard WGS84 UTM zones are supported."), ProjectedCSType);
			return false;
		}
	}
	else
	{
		OutError = TEXT("Unsupported or missing GTModelTypeGeoKey -- expected Geographic or Projected.");
		return false;
	}

	UE_LOG(LogTerraDyne, Log, TEXT("GeoTIFF loaded: %dx%d, elevation [%.1f, %.1f]m, %s"),
		Width, Height, MinZ, MaxZ,
		OutRaster.bIsGeographicWGS84 ? TEXT("WGS84 geographic")
			: (OutRaster.bIsUTM ? *FString::Printf(TEXT("UTM zone %d%s"), OutRaster.UTMZone, OutRaster.bUTMNorth ? TEXT("N") : TEXT("S")) : TEXT("unknown CRS")));

	return true;
}
