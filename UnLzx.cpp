//////////////////////////////////////
//
// UnLzx.cpp
//
// Ilkka Prusi
// ilkka.prusi@gmail.com
//
// Based on: 
// unlzx.c 1.1 (03.4.01, Erik Meusel)
//

#include "UnLzx.h"



// TODO: make buffers variable in size..

unsigned char read_buffer[16384]; /* have a reasonable sized read buffer */
unsigned char decrunch_buffer[258+65536+258]; /* allow overrun for speed */

unsigned char *source;
unsigned char *destination;
unsigned char *source_end;
unsigned char *destination_end;

unsigned int g_decrunch_method = 0;
unsigned int g_decrunch_length = 0;

unsigned char offset_len[8];
unsigned short offset_table[128];
unsigned char huffman20_len[20];
unsigned short huffman20_table[96];
unsigned char literal_len[768];
unsigned short literal_table[5120];

static const unsigned char table_one[32]=
{
 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14
};

static const unsigned int table_two[32]=
{
 0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,1024,
 1536,2048,3072,4096,6144,8192,12288,16384,24576,32768,49152
};

static const unsigned int table_three[16]=
{
 0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767
};

static const unsigned char table_four[34]=
{
 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
};


/* reverse the order of the position's bits */
inline unsigned int reverse_position(const unsigned int fill_size, const unsigned int reverse_pos)
{
	unsigned int fill = fill_size;
	unsigned int reverse = reverse_pos;
	unsigned int leaf = 0; /* always starts at zero */
	
	/* reverse the position */
	do
	{
		leaf = (leaf << 1) + (reverse & 1);
		reverse >>= 1;
	} while(--fill);
	
	/* used after reversing*/
	return leaf;
}

/* Build a fast huffman decode table from the symbol bit lengths.         */
/* There is an alternate algorithm which is faster but also more complex. */
int make_decode_table(int number_symbols, int table_size, unsigned char *length, unsigned short *table)
{
	unsigned char bit_num = 1; // truly start at 1 (skip one increment..)
	unsigned int pos = 0; /* consistantly used as the current position in the decode table */
	unsigned int table_mask = (1 << table_size);
	unsigned int bit_mask = (table_mask >> 1); // don't do the first number (note: may zero bits in shifting)

	while (bit_num <= table_size)
	{
		for(int symbol = 0; symbol < number_symbols; symbol++)
		{
			if(length[symbol] == bit_num)
			{
				/* reverse the order of the position's bits */
				unsigned int leaf = reverse_position(table_size, pos);
				
				if((pos += bit_mask) > table_mask)
				{
					//throw IOException("overrun table!");
					return 1; /* we will overrun the table! abort! */
				}
				
				unsigned int fill = bit_mask;
				unsigned int next_symbol = 1 << bit_num;
				do
				{
					table[leaf] = symbol;
					leaf += next_symbol;
				} while(--fill);
			}
		}
		bit_mask >>= 1;
		bit_num++;
	}

	if (pos != table_mask)
	{
		for(int symbol = pos; symbol < table_mask; symbol++) /* clear the rest of the table */
		{
			/* reverse the order of the position's bits */
			unsigned int leaf = reverse_position(table_size, symbol);
			table[leaf] = 0;
		}
		
		unsigned int next_symbol = table_mask >> 1;
		pos <<= 16;
		table_mask <<= 16;
		
		// unconditional set value -> make scoped
		unsigned int bit_mask = 32768;

		while (bit_num <= 16)
		{
			for(int symbol = 0; symbol < number_symbols; symbol++)
			{
				if(length[symbol] == bit_num)
				{
					/* reverse the order of the position's bits */
					unsigned int leaf = reverse_position(table_size, pos >> 16);
					for(unsigned int fill = 0; fill < bit_num - table_size; fill++)
					{
						if(table[leaf] == 0)
						{
							table[(next_symbol << 1)] = 0;
							table[(next_symbol << 1) + 1] = 0;
							table[leaf] = next_symbol++;
						}
						leaf = table[leaf] << 1;
						leaf += (pos >> (15 - fill)) & 1;
					}
					
					table[leaf] = symbol;
					if((pos += bit_mask) > table_mask)
					{
						//throw IOException("overrun table incomplete!");
						return 1; /* we will overrun the table! abort! */
					}
				}
			}
			bit_mask >>= 1;
			bit_num++;
		}
	}

	if (pos != table_mask) 
	{
		// TODO: throw exception instead?
		//throw IOException("table is incomplete!");
		return 1; /* the table is incomplete! */
	}
	return 0;
}

// TODO: change names of globals later..
// (hint compiler to inline function, can't force though..)
inline void fix_shift_control_word(int &shift, unsigned int &control)
{
	shift += 16;
	control += *source++ << (8 + shift);
	control += *source++ << shift;
}

inline void fix_shift_control_long(int &shift, unsigned int &control)
{
	shift += 16;
	control += *source++ << 24;
	control += *source++ << 16;
}

// two cases only but shortens read_literal_table() slightly..
void symbol_longer_than_6_bits(int &shift, unsigned int &control, unsigned int &symbol)
{
	/* when symbol is longer than 6 bits */
	do
	{
		symbol = huffman20_table[((control >> 6) & 1) + (symbol << 1)];
		if(!shift--)
		{
			fix_shift_control_long(shift, control);
		}
		control >>= 1;
	} while(symbol >= 20);
}

// just shortens read_literal_table() slightly..
void read_decrunch_length(int &shift, unsigned int &control, unsigned int &decrunch_length)
{
	decrunch_length = (control & 255) << 16;
	
	control >>= 8;
	if((shift -= 8) < 0)
	{
		fix_shift_control_word(shift, control);
	}
	
	decrunch_length += (control & 255) << 8;

	control >>= 8;
	if((shift -= 8) < 0)
	{
		fix_shift_control_word(shift, control);
	}
	
	decrunch_length += (control & 255);

	control >>= 8;
	if((shift -= 8) < 0)
	{
		fix_shift_control_word(shift, control);
	}
}

int read_build_offset_table(int &shift, unsigned int &control)
{
	unsigned int temp = 0;
	for(temp = 0; temp < 8; temp++)
	{
		offset_len[temp] = control & 7;
		control >>= 3;
		if((shift -= 3) < 0)
		{
			fix_shift_control_word(shift, control);
		}
	}
	return make_decode_table(8, 7, offset_len, offset_table);
}

/* Read and build the decrunch tables. There better be enough data in the */
/* source buffer or it's stuffed. */
int read_literal_table(unsigned int &control, int &shift, unsigned int &decrunch_method, unsigned int &decrunch_length)
{
	/* fix the control word if necessary */
	if (shift < 0)
	{
		fix_shift_control_word(shift, control);
	}

	/* read the decrunch method */
	decrunch_method = control & 7;
	control >>= 3;
	if((shift -= 3) < 0)
	{
		fix_shift_control_word(shift, control);
	}

	/* Read and build the offset huffman table */
	if(decrunch_method == 3)
	{
		if (read_build_offset_table(shift, control) != 0)
		{
			return 1;
		}
	}

	/* read decrunch length */
	read_decrunch_length(shift, control, decrunch_length);

	/* read and build the huffman literal table */
	if (decrunch_method != 1)
	{
		// only used in this scope..
		unsigned int pos = 0;
		unsigned int fix = 1;
		unsigned int max_symbol = 256;
		unsigned int temp = 0;

		unsigned int count = 0;
		
		do
		{
			for(temp = 0; temp < 20; temp++)
			{
				huffman20_len[temp] = control & 15;
				control >>= 4;
				if((shift -= 4) < 0)
				{
					fix_shift_control_word(shift, control);
				}
			}
			
			if (make_decode_table(20, 6, huffman20_len, huffman20_table) != 0)
			{
				//throw IOException("table is corrupt!");
				//break; /* argh! table is corrupt! */
				return 1;
			}

			do
			{
				unsigned int symbol = huffman20_table[control & 63];
				if(symbol >= 20)
				{
					symbol_longer_than_6_bits(shift, control, symbol);
					temp = 6;
				}
				else
				{
					temp = huffman20_len[symbol];
				}
				
				control >>= temp;
				if((shift -= temp) < 0)
				{
					fix_shift_control_word(shift, control);
				}

				switch(symbol)
				{
				case 17:
				case 18:
				{
					if(symbol == 17)
					{
						temp = 4;
						count = 3;
					}
					else /* symbol == 18 */
					{
						temp = 6 - fix;
						count = 19;
					}
					
					count += (control & table_three[temp]) + fix;
					control >>= temp;
					if((shift -= temp) < 0)
					{
						fix_shift_control_word(shift, control);
					}
					
					while((pos < max_symbol) && (count--))
					{
						literal_len[pos++] = 0;
					}
					break;
				} // case 17, case 18

				case 19:
				{
					count = (control & 1) + 3 + fix;
					if(!shift--)
					{
						fix_shift_control_long(shift, control);
					}
					
					control >>= 1;
					symbol = huffman20_table[control & 63];
					if(symbol >= 20)
					{
						symbol_longer_than_6_bits(shift, control, symbol);
						temp = 6;
					}
					else
					{
						temp = huffman20_len[symbol];
					}
					
					control >>= temp;
					if((shift -= temp) < 0)
					{
						fix_shift_control_word(shift, control);
					}
					
					symbol = table_four[literal_len[pos] + 17 - symbol];
					while((pos < max_symbol) && (count--))
					{
						literal_len[pos++] = symbol;
					}
					break;
				} // case 19

				default:
				{
					symbol = table_four[literal_len[pos] + 17 - symbol];
					literal_len[pos++] = symbol;
					break;
				} // default
				} // switch
			} while(pos < max_symbol);
			
			fix--;
			max_symbol += 512;
		} while(max_symbol == 768);

		if (make_decode_table(768, 12, literal_len, literal_table) != 0)
		{
			return 1;
		}
	}

	return 0;
}

/* Fill up the decrunch buffer. Needs lots of overrun for both destination */
/* and source buffers. Most of the time is spent in this routine so it's  */
/* pretty damn optimized. */
void decrunch(unsigned int &control, int &shift, unsigned int &last_offset, unsigned int &decrunch_method)
{
	do
	{
		unsigned int symbol = literal_table[control & 4095];
		if(symbol >= 768)
		{
			control >>= 12;
			if((shift -= 12) < 0)
			{
				fix_shift_control_word(shift, control);
			}
			
			/* literal is longer than 12 bits */
			do
			{
				symbol = literal_table[(control & 1) + (symbol << 1)];
				if(!shift--)
				{
					fix_shift_control_long(shift, control);
				}
				control >>= 1;
			} while(symbol >= 768);
		}
		else
		{
			unsigned int temp = literal_len[symbol];
			control >>= temp;
			if((shift -= temp) < 0)
			{
				fix_shift_control_word(shift, control);
			}
		}

		if(symbol < 256)
		{
			*destination++ = symbol;
		}
		else
		{
			symbol -= 256;
			
			unsigned int temp = symbol & 31;
			unsigned int count = table_two[temp];
			temp = table_one[temp];
			if((temp >= 3) && (decrunch_method == 3))
			{
				temp -= 3;
				count += ((control & table_three[temp]) << 3);
				control >>= temp;
				if((shift -= temp) < 0)
				{
					fix_shift_control_word(shift, control);
				}
				temp = offset_table[control & 127];
				count += temp;
				temp = offset_len[temp];
			}
			else
			{
				count += control & table_three[temp];
				if(!count)
				{
					count = last_offset;
				}
			}
			
			control >>= temp;
			if((shift -= temp) < 0)
			{
				fix_shift_control_word(shift, control);
			}
			last_offset = count;

			temp = (symbol >> 5) & 15;
			count = table_two[temp] + 3;
			temp = table_one[temp];
			count += (control & table_three[temp]);
			control >>= temp;
			
			if((shift -= temp) < 0)
			{
				fix_shift_control_word(shift, control);
			}

			unsigned char *string = (decrunch_buffer + last_offset < destination) ?
					destination - last_offset : 
					destination + 65536 - last_offset;

			// note: in theory could replace loop with
			// ::memcpy(destination, string, count);
			// but may overlap memory areas so can't use memcpy()..
			do
			{
				*destination++ = *string++;
			} while(--count);
		}
	} while((destination < destination_end) && (source < source_end));
}

///////////// CUnLzx

// create path and open file for writing
void CUnLzx::PrepareEntryForWriting(CArchiveEntry &Entry, CAnsiFile &OutFile)
{
	// this should have proper path-ending already..
	std::string szTempPath = m_szExtractionPath;
	szTempPath += Entry.m_szFileName;

	if (CPathHelper::MakePathToFile(szTempPath) == false)
	{
		// ignore only?
		// -> should fail when creating file there..
	}

	// open for writing
	if (OutFile.Open(szTempPath, true) == false)
	{
		// failed to open for writing
		// -> exception (critical error)
		throw ArcException("FOpen(Out)", szTempPath);
	}
}

void CUnLzx::OpenArchiveFile(CAnsiFile &ArchiveFile)
{
	if (ArchiveFile.Open(m_szArchive) == false)
	{
		throw IOException("FOpen(Archive)");
	}

	m_nFileSize = ArchiveFile.GetSize();
	if (m_nFileSize < sizeof(tLzxInfoHeader))
	{
		// (just ignore like old one?)
		throw ArcException("Not enough data to read header", m_szArchive);
	}

	if (ArchiveFile.Read(m_InfoHeader.info_header, 10) == false)
	{
		throw ArcException("Not enough data to read info header", m_szArchive);
	}

	if (m_InfoHeader.IsLzx() == false)
	{
		throw ArcException("Info_Header: Bad ID", m_szArchive);
	}
}

bool CUnLzx::ReadEntryHeader(CAnsiFile &ArchiveFile, CArchiveEntry &Entry)
{
	// read entry header from archive
	if (ArchiveFile.Read(Entry.m_Header.archive_header, 31) == false)
	{
		// could not read header
		// -> no more files in archive?
		return false;
	}

	// temp for counting crc-checksum of entry-header,
	// verify by counting that crc in file is same
	CRCSum CrcSum; // TODO: member of entry?
	
	// get value and reset for counting crc (set zero where header CRC):
	// count CRC of this portion with zero in CRC-bytes
	Entry.m_uiCrc = Entry.m_Header.TakeCrcBytes();
	CrcSum.crc_calc(Entry.m_Header.archive_header, 31);

	// keep data-CRC for easy handling
	Entry.m_uiDataCrc = Entry.m_Header.GetDataCrc();

	// zeroize buffer
	m_ReadBuffer.PrepareBuffer(256, false);
	unsigned char *pBuf = m_ReadBuffer.GetBegin();
	unsigned int uiStringLen = 0; // temp for reading string
	
	// read file name
	uiStringLen = Entry.m_Header.GetFileNameLength();
	if (ArchiveFile.Read(pBuf, uiStringLen) == false)
	{
		throw IOException("Failed reading string: filename");
	}

	pBuf[uiStringLen] = 0;                 // null-terminate
	CrcSum.crc_calc(pBuf, uiStringLen);    // update CRC
	Entry.m_szFileName = (char*)pBuf;      // keep as string

	// read comment
	uiStringLen = Entry.m_Header.GetCommentLength();
	if (ArchiveFile.Read(pBuf, uiStringLen) == false)
	{
		throw IOException("Failed reading string: comment");
	}

	pBuf[uiStringLen] = 0;                 // null-terminate
	CrcSum.crc_calc(pBuf, uiStringLen);    // update CRC
	Entry.m_szComment = (char*)pBuf;       // keep as string

	// verify counted crc against the one read from file
	// (in case of corruption of file),
	// check against CRC in entry-header (instead of data which is separate)
	if (CrcSum.GetSum() != Entry.m_uiCrc)
	{
		// critical error? 
		// -> throw exception
		throw ArcException("CRC: Entry Header", Entry.m_szFileName);
	}

	// parse some entry-header information for later processing
	Entry.ParseAttributes();	// file protection modes
	Entry.HandlePackingSizes(); // packed/unpacked and merge-cases
	
	// entry header read
	return true;
}

bool CUnLzx::ViewArchive(CAnsiFile &ArchiveFile)
{
	// reset statistical counters before adding again
	ResetCounters();
	
	long lEntryOffset = 0;
	while (lEntryOffset < ArchiveFile.GetSize())
	{
		if (ArchiveFile.Tell(lEntryOffset) == false)
		{
			throw IOException("FTell(): failed to tell position");
		}

		auto itEntry = m_EntryList.find(lEntryOffset); // try to locate this..
		if (itEntry == m_EntryList.end())
		{
			// add mapping of this offset to entry-information
			m_EntryList.insert(tArchiveEntryList::value_type(lEntryOffset,CArchiveEntry()));
			itEntry = m_EntryList.find(lEntryOffset); // locate it again
		}
		
		/*
		if ((lEntryOffset + 31) >= ArchiveFile.GetSize())
		{
			// near or at end of file -> no more headers
			return true;
		}
		*/
		
		// read and verify checksum of this entry-header,
		// should throw exception on error
		if (ReadEntryHeader(ArchiveFile, itEntry->second) == false)
		{
			// entry header could not be read
			// -> no more files in archive
			return true;
		}

		// count some statistical information
		AddCounters(itEntry->second);
		
		// TODO: if file starts a merged group, add info to list
		/*
		m_GroupList.insert(tMergeGroupList::value_type(lEntryOffset, CMergeGroup()));
		auto itGroup = m_GroupList.find(lEntryOffset);
		CMergeGroup *pGroup = &(itGroup->second);
		itEntry->second.SetGroup(pGroup);
		*/

		/* seek past the packed data */
		if (itEntry->second.m_ulPackedSize)
		{
			m_ulMergeSize = 0; // why?
			if (ArchiveFile.Seek(itEntry->second.m_ulPackedSize, SEEK_CUR) == false)
			{
				throw IOException("FSeek(): failed to seek past packed data");
			}
		}
	}

	return true;
}

// this is actual "decompress+store" method on compressed files?
// (ExtractStore() for unpacked+store?)
//
bool CUnLzx::ExtractNormal(CAnsiFile &ArchiveFile)
{
	::memset(offset_len, 0, sizeof(unsigned char)*8);
	::memset(literal_len, 0, sizeof(unsigned char)*768);

	// setup some globals
	source = read_buffer + 16384;
	source_end = source - 1024;
	destination = decrunch_buffer + 258 + 65536;
	destination_end = destination;
	
	unsigned int last_offset = 1;
	unsigned int global_control = 0; /* initial control word */
	int global_shift = -16;
	unsigned char *pos = destination_end;

	// TODO: use merged group information in extraction
	// TODO: check that already extracted isn't handled again?
	auto itEntry = m_EntryList.begin();
	while (itEntry != m_EntryList.end())
	{
		CArchiveEntry &Entry = itEntry->second;

		// is extracted already?
		// TODO: think of better way for merged files..
		if (Entry.m_bIsExtracted == true)
		{
			++itEntry;
			continue;
		}

		// verify path exists and create it if necessary,
		// open file for writing
		// (should throw exception if critical..)
		CAnsiFile OutFile;
		PrepareEntryForWriting(Entry, OutFile);

		CRCSum CrcSum; // TODO: member of entry?
		
		unsigned int count = 0;
		unsigned int unpack_size = Entry.m_ulUnpackedSize;

		while (unpack_size > 0)
		{
			if(pos == destination) /* time to fill the buffer? */
			{
				/* check if we have enough data and read some if not */
				if(source >= source_end) /* have we exhausted the current read buffer? */
				{
					unsigned char *temp = read_buffer;
					count = temp - source + 16384;
					if(count)
					{
						/* copy the remaining overrun to the start of the buffer */
						// note: in theory could use memcpy
						// but memory-areas may be overlapping partially
						// -> can't use memcpy() safely..
						do
						{
							*temp++ = *source++;
						} while(--count);
					}
					source = read_buffer;
					count = source - temp + 16384;

					if (m_pack_size < count) 
					{
						count = m_pack_size; /* make sure we don't read too much */
					}
					if (ArchiveFile.Read(temp, count) == false)
					{
						throw IOException("FRead(Archive)");
					}
					m_pack_size -= count;
					temp += count;
					if(source >= temp) 
					{
						break; /* argh! no more data! */
					}
				} /* if(source >= source_end) */

				/* check if we need to read the tables */
				if (g_decrunch_length <= 0)
				{
					if (read_literal_table(global_control, global_shift, g_decrunch_method, g_decrunch_length))
					{
						/* argh! can't make huffman tables! */
						throw IOException("can't make huffman tables!");
						//break;
					}
				}
				/* unpack some data */
				if(destination >= decrunch_buffer + 258 + 65536)
				{
					count = (destination - decrunch_buffer) - 65536;
					if(count)
					{
						destination = decrunch_buffer;
						unsigned char *temp = destination + 65536;
						/* copy the overrun to the start of the buffer */
						// note: in theory could replace loop with
						// ::memcpy(destination, temp, count);
						// but may overlap memory areas so can't use memcpy()..
						do
						{
							*destination++ = *temp++;
						} while(--count);
					}
					pos = destination;
				}
				
				destination_end = destination + g_decrunch_length;
				if(destination_end > decrunch_buffer + 258 + 65536)
				{
					destination_end = decrunch_buffer + 258 + 65536;
				}
				
				unsigned char *temp = destination;

				decrunch(global_control, global_shift, last_offset, g_decrunch_method);

				g_decrunch_length -= (destination - temp);
			} /* if(pos == destination) */

			/* calculate amount of data we can use before we need to fill the buffer again */
			count = destination - pos;
			if(count > unpack_size) 
			{
				count = unpack_size; /* take only what we need */
			}

			CrcSum.crc_calc(pos, count);

			if (OutFile.Write(pos, count) == false)
			{
				throw IOException("FWrite(Out)");
			}
			unpack_size -= count;
			pos += count;
		} // while (unpack_size > 0)

		// just make sure enough written before closing
		if (OutFile.Flush() == false)
		{
			throw ArcException("Failed to flush on output-entry", Entry.m_szFileName);
		}
		OutFile.Close();

		// note: data has different CRC
		// which we need to use in here,
		// header of file-entry has another in the archive
		//
		if (CrcSum.GetSum() != Entry.m_uiDataCrc)
		{
			throw ArcException("CRC: Entry Data", Entry.m_szFileName);
		}

		// extracted
		// TODO: think of better way for merged files..
		Entry.m_bIsExtracted = true;

		++itEntry;
	} // while (itEntry != m_EntryList.end())

	return true;
}

// this is actually store "as-is" method when no compression applied?
//
bool CUnLzx::ExtractStore(CAnsiFile &ArchiveFile)
{
	// TODO: check that already extracted isn't handled again?
	auto itEntry = m_EntryList.begin();
	while (itEntry != m_EntryList.end())
	{
		CArchiveEntry &Entry = itEntry->second;

		// verify path exists and create it if necessary,
		// open file for writing
		// (should throw exception if critical..)
		CAnsiFile OutFile;
		PrepareEntryForWriting(Entry, OutFile);

		CRCSum CrcSum; // TODO: member of entry?
		unsigned int unpack_size = Entry.m_ulUnpackedSize;

		// why this?
		// in this method of packing there is no compression
		// -> only read&write same amount as actually is in archive..
		// (case for a corrupted file?)
		//
		if (unpack_size > Entry.m_ulPackedSize) 
		{
			unpack_size = Entry.m_ulPackedSize;
		}
		
		// prepare buffer to at least given size
		// (keep if it is larger, allocate if not),
		// zero existing memory
		m_ReadBuffer.PrepareBuffer(16384, false);
		size_t nBufSize = m_ReadBuffer.GetSize();
		unsigned char *pReadBuf = m_ReadBuffer.GetBegin();

		while (unpack_size > 0)
		{
			unsigned int count = (unpack_size > nBufSize) ? nBufSize : unpack_size;

			// must succeed reading wanted
			if (ArchiveFile.Read(pReadBuf, count) == false)
			{
				throw IOException("Failed to read entry from archive");
			}
			m_pack_size -= count;

			// count crc
			CrcSum.crc_calc(pReadBuf, count);

			if (OutFile.Write(pReadBuf, count) == false)
			{
				throw ArcException("Failed to write to output-entry", Entry.m_szFileName);
			}
			unpack_size -= count;
		}

		// just make sure enough written before closing
		if (OutFile.Flush() == false)
		{
			throw ArcException("Failed to flush on output-entry", Entry.m_szFileName);
		}
		OutFile.Close();

		// use CRC of data when checking data..
		//
		if (CrcSum.GetSum() != Entry.m_uiDataCrc)
		{
			throw ArcException("CRC: Entry Data", Entry.m_szFileName);
		}

		++itEntry;
	}
	return true;
}

// actually, just place-holder in case we can't understand the packing mode..
//
bool CUnLzx::ExtractUnknown(CAnsiFile &ArchiveFile)
{
	throw IOException("Extract: unknown pack mode");
	return true;
}

bool CUnLzx::ExtractArchive()
{
	CAnsiFile ArchiveFile;
	OpenArchiveFile(ArchiveFile);

	// reset statistical counters before adding again
	ResetCounters();
	
	long lEntryOffset = 0;
	while (lEntryOffset < ArchiveFile.GetSize())
	{
		if (ArchiveFile.Tell(lEntryOffset) == false)
		{
			throw IOException("FTell(): failed to tell position");
		}

		// TODO: for merged files, use temporary list of each entry
		// so that we only extract those in same merge
		// and not twice any file already extracted?
		// TODO: use merged group information in extraction

		auto itEntry = m_EntryList.find(lEntryOffset); // try to locate this..
		if (itEntry == m_EntryList.end())
		{
			// add mapping of this offset to entry-information
			m_EntryList.insert(tArchiveEntryList::value_type(lEntryOffset,CArchiveEntry()));
			itEntry = m_EntryList.find(lEntryOffset); // locate it again
		}
		
		/*
		if ((lEntryOffset + 31) >= ArchiveFile.GetSize())
		{
			// near or at end of file -> no more headers
			return true;
		}
		*/

		// read and verify checksum of this entry-header,
		// should throw exception on error
		if (ReadEntryHeader(ArchiveFile, itEntry->second) == false)
		{
			// entry header could not be read
			// -> no more files in archive
			return true;
		}

		// count some statistical information
		AddCounters(itEntry->second);

		/* seek past the packed data */
		m_pack_size = itEntry->second.m_Header.GetPackSize();
		if (m_pack_size != 0)
		{
			bool bRet = false;
			switch (itEntry->second.m_Header.GetPackMode())
			{
			case 0: /* store */
				bRet = ExtractStore(ArchiveFile);
				break;
			case 2: /* normal */
				bRet = ExtractNormal(ArchiveFile);
				break;
			default: /* unknown */
				bRet = ExtractUnknown(ArchiveFile);
				break;
			}

			// in case of merged files, need to seek data from next entry
			if (ArchiveFile.Seek(m_pack_size, SEEK_CUR) == false)
			{
				throw IOException("FSeek(Data): failed to seek past packed data");
			}
		}
	} 

	return true;
}


//////// public methods

// list all files in archive for viewing
bool CUnLzx::View(tArchiveEntryList &lstArchiveInfo)
{
	CAnsiFile ArchiveFile;

	OpenArchiveFile(ArchiveFile);
	if (ViewArchive(ArchiveFile) == true)
	{
		lstArchiveInfo = m_EntryList;
		return true;
	}
	return false;
}

// extract all files from archive to given path
bool CUnLzx::Extract(const std::string &szOutPath)
{
	// allow resetting on another extract..
	m_szExtractionPath = szOutPath;
	if (m_szExtractionPath.length() > 0)
	{
		// replace "\\" by "/"..
		//m_szExtractionPath.replace('\\', "/");
		auto it = m_szExtractionPath.begin();
		auto itEnd = m_szExtractionPath.end();
		while (it != itEnd)
		{
			if ((*it) == '\\')
			{
				(*it) = '/';
			}
			++it;
		}

		// check proper ending..
		if (m_szExtractionPath.at(m_szExtractionPath.length() -1) != '/')
		{
			m_szExtractionPath += "/";
		}

		// note: check that output-path exists first,
		// check that it is used by file store also..
		CPathHelper::MakePath(m_szExtractionPath);
	}
	return ExtractArchive();
}

/*
// verify integrity of archive-file
bool CUnLzx::TestArchive()
{
}
*/
