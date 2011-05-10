//////////////////////////////////////
//
// UnLzx.h
//
// Ilkka Prusi
// ilkka.prusi@gmail.com
//

#ifndef _UNLZX_H_
#define _UNLZX_H_

#include <string>
//#include <list>
#include <map>
#include <set>

#include "AnsiFile.h"


// archive info-header entry (archive-file header)
struct tLzxInfoHeader
{
	unsigned char info_header[10];

	tLzxInfoHeader()
	{
		::memset(info_header, 0, sizeof(unsigned char)*10);
	}

	bool IsLzx()
	{
		if ((info_header[0] == 76) && (info_header[1] == 90) && (info_header[2] == 88)) /* LZX */
		{
			return true;
		}
		return false;
	}
};

// header of single entry in LZX-archive
struct tLzxArchiveHeader
{
	unsigned char archive_header[31];

	tLzxArchiveHeader()
	{
		::memset(archive_header, 0, sizeof(unsigned char)*31);
	}

	// CRC in header (including filename, comment etc.):
	// set to zero before counting CRC of this part
	unsigned int TakeCrcBytes()
	{
		unsigned int crc = (archive_header[29] << 24) + (archive_header[28] << 16) + (archive_header[27] << 8) + archive_header[26];

		/* Must set the field to 0 before calculating the crc */
		archive_header[29] = 0;
		archive_header[28] = 0;
		archive_header[27] = 0;
		archive_header[26] = 0;

		return crc;
	}

	// CRC of actual data (after checking header-CRC)
	unsigned int GetDataCrc()
	{
		return (archive_header[25] << 24) + (archive_header[24] << 16) + (archive_header[23] << 8) + archive_header[22]; /* data crc */
	}

	unsigned char GetAttributes()
	{
		return archive_header[0]; /* file protection modes */
	}

	unsigned char GetPackMode()
	{
		return archive_header[11]; /* pack mode */
	}

	unsigned int GetFileNameLength()
	{
		return archive_header[30]; /* filename length */
	}

	unsigned int GetCommentLength()
	{
		return archive_header[14]; /* comment length */
	}

	unsigned int GetPackSize()
	{
		return (archive_header[9] << 24) + (archive_header[8] << 16) + (archive_header[7] << 8) + archive_header[6]; /* packed size */
	}

	unsigned int GetUnpackSize()
	{
		return (archive_header[5] << 24) + (archive_header[4] << 16) + (archive_header[3] << 8) + archive_header[2]; /* unpack size */
	}

	unsigned int GetTimestamp()
	{
		return (archive_header[18] << 24) + (archive_header[19] << 16) + (archive_header[20] << 8) + archive_header[21]; /* date */
	}

	void GetTimestampParts(unsigned int &year, unsigned int &month, unsigned int &day, unsigned int &hour, unsigned int &minute, unsigned int &second)
	{
		unsigned int temp = GetTimestamp();

		// split value into timestamp-parts
		year = ((temp >> 17) & 63) + 1970;
		month = (temp >> 23) & 15;
		day = (temp >> 27) & 31;
		hour = (temp >> 12) & 31;
		minute = (temp >> 6) & 63;
		second = temp & 63;
	}
};

// fwd. decl.
class CArchiveEntry;

class CMergeGroup
{
public:
	CMergeGroup(void) 
		: m_MergedList()
		, m_lGroupOffset(0)
	{};
	~CMergeGroup(void) 
	{
		// don't delete objects, 
		// destroyed elsewhere.
		m_MergedList.clear();
	};
	
	typedef std::set<CArchiveEntry*> tMergedList;
	tMergedList m_MergedList;

	// offset in archive file
	// to beginning of this group of merged files.
	long m_lGroupOffset;
	
	// add entry as member of this group
	void SetEntry(CArchiveEntry *pEntry)
	{
		auto it = m_MergedList.find(pEntry);
		if (it == m_MergedList.end())
		{
			m_MergedList.insert(tMergedList::value_type(pEntry));
		}
	}
};

// describe single entry within LZX-archive
class CArchiveEntry
{
public:
	// these match Amiga-style file-attributes
	// (protection mode flags):
	// HSPA RWED
	struct tFileAttributes
	{
		tFileAttributes()
		{
			h = false;
			s = false;
			p = false;
			a = false;

			r = false;
			w = false;
			e = false;
			d = false;
		}

		bool h; // 'hidden'
		bool s; // 'script'
		bool p; // 'pure' (can be made resident in-memory)
		bool a; // 'archived'
		bool r; // 'readable'
		bool w; // 'writable'
		bool e; // 'executable'
		bool d; // 'delete'
	};
	struct tFileAttributes m_Attributes;

public:
	CArchiveEntry(void)
		: m_Attributes()
		, m_Header()
		, m_pGroup(nullptr)
		, m_uiCrc(0)
		, m_uiDataCrc(0)
		, m_ulUnpackedSize(0)
		, m_bPackedSizeAvailable(true)
		, m_ulPackedSize(0)
		, m_bIsMerged(false)
		, m_bIsExtracted(false)
		, m_szFileName()
		, m_szComment()
	{}
	~CArchiveEntry(void)
	{}

	/* file protection modes */
	void ParseAttributes()
	{
		unsigned char attrib = m_Header.GetAttributes();
		m_Attributes.h = ((attrib & 32) ? true : false);
		m_Attributes.s = ((attrib & 64) ? true : false);
		m_Attributes.p = ((attrib & 128) ? true : false);
		m_Attributes.a = ((attrib & 16) ? true : false);
		m_Attributes.r = ((attrib & 1) ? true : false);
		m_Attributes.w = ((attrib & 2) ? true : false);
		m_Attributes.e = ((attrib & 8) ? true : false);
		m_Attributes.d = ((attrib & 4) ? true : false);
	};
	void HandlePackingSizes()
	{
		m_ulUnpackedSize = m_Header.GetUnpackSize();
		m_ulPackedSize = m_Header.GetPackSize();

		if (m_Header.archive_header[12] & 1)
		{
			m_bPackedSizeAvailable = false;
		}
		if ((m_Header.archive_header[12] & 1) && m_ulPackedSize)
		{
			m_bIsMerged = true;
		}
	}
	void SetGroup(CMergeGroup *pGroup)
	{
		// keep group and set as member of it
		m_pGroup = pGroup;
		pGroup->SetEntry(this);
	}

	// entry header from archive
	tLzxArchiveHeader m_Header;
	
	// merge group this belongs to (if any)
	CMergeGroup *m_pGroup;

	// CRC from file header
	unsigned int m_uiCrc;

	// CRC of data
	unsigned int m_uiDataCrc;

	// unpacked size from file
	unsigned long m_ulUnpackedSize;

	// for some files, packed size might not be given in archive
	// (merged files only?)
	bool m_bPackedSizeAvailable;

	// compressed size from file
	unsigned long m_ulPackedSize;

	// if file is merged with another
	bool m_bIsMerged;

	// is file already extracted
	// TODO: this is quick hack, 
	// think of better way to handle merged files..
	bool m_bIsExtracted;

	// name of entry from archive
	std::string m_szFileName;

	// comment of entry from archive
	std::string m_szComment;
};

// list of each merged-file group in single archive
// key: offset in archive-file to group (internal use)
// value: description of group
typedef std::map<long, CMergeGroup> tMergeGroupList;

// list of each entry in single archive
// key: offset in archive-file to entry (internal use)
// value: description of entry
typedef std::map<long, CArchiveEntry> tArchiveEntryList;

/*  moved to CAnsiFile.
class CReadBuffer
*/

/* // TODO: move crc-summing to own class
class CRCSum
{
	static const unsigned int g_crc_table[256];
	inline void crc_calc(const unsigned char *memory, unsigned int length, unsigned int &sum) const;
	
};
*/

/* // TODO: move actual decoding to own class (per compression type)
class CDecoder
{
};
*/

class CUnLzx
{
private:

	std::string m_szArchive; // path and name of archive-file
	size_t m_nFileSize; // filesize of archive in bytes

	// internal buffer for read information
	CReadBuffer m_ReadBuffer;
	
	// archive info-header (file-type etc.)
	tLzxInfoHeader m_InfoHeader;

	// list of merged-file groups in archive
	tMergeGroupList m_GroupList;
	
	// list of items in archive (files)
	tArchiveEntryList m_EntryList;

	// user-given path where file(s) are 
	// extracted to from current archive
	// (may change on each extract() call..)
	std::string m_szExtractionPath;

	// current sizes for extraction
	// (need to share for merged groups)
	unsigned int m_pack_size;
	//unsigned int m_unpack_size;

	// some counters for statistics of archive
	unsigned long m_ulTotalUnpacked;
	unsigned long m_ulTotalPacked;
	unsigned long m_ulTotalFiles;
	unsigned long m_ulMergeSize;

	// update counters with those in entry
	void AddCounters(CArchiveEntry &Entry)
	{
		// add some statistical information
		m_ulTotalPacked += Entry.m_ulPackedSize;
		m_ulTotalUnpacked += Entry.m_ulUnpackedSize;
		m_ulMergeSize += Entry.m_ulUnpackedSize;
		m_ulTotalFiles++;
	}
	void ResetCounters()
	{
		m_ulTotalUnpacked = 0;
		m_ulTotalPacked = 0;
		m_ulTotalFiles = 0;
		m_ulMergeSize = 0;
	}

protected:
	// create path and open file for writing
	void PrepareEntryForWriting(CArchiveEntry &Entry, CAnsiFile &OutFile);

	void OpenArchiveFile(CAnsiFile &ArchiveFile);

	void ReadEntryHeader(CAnsiFile &ArchiveFile, CArchiveEntry &Entry);

	bool ViewArchive(CAnsiFile &ArchiveFile);

	bool ExtractNormal(CAnsiFile &ArchiveFile);
	bool ExtractStore(CAnsiFile &ArchiveFile);
	bool ExtractUnknown(CAnsiFile &ArchiveFile);

	bool ExtractArchive();

public:
	// options for extraction (some for viewing?)
	class CUnLzxOptions
	{
		bool m_bKeepAttributes;		// keep file-attribs (set on output-file)
		bool m_bKeepTime;			// keep timestamp (set on output-file)
		bool m_bClearArcBit;		// clear "archive" flag on extract
		bool m_bLowercaseFilenames; // make filenames lower-case on extract

		// by default, we handle paths "as-is",
		// with this no paths are created on extraction (all to same place)
		// which can cause trouble with duplicate filenames..
		//bool m_bIgnorePaths;		// don't preserve path names on extract

		//std::string m_szPattern; // pattern-match to files (extract matching)
		//std::string m_szWorkPath; // set working path
	};

	CUnLzx(const std::string &szArchive)
		: m_szArchive(szArchive)
		, m_nFileSize(0)
		, m_ReadBuffer()
		, m_InfoHeader()
		, m_GroupList()
		, m_EntryList()
		, m_szExtractionPath()
		, m_pack_size(0)
		, m_ulTotalUnpacked(0)
		, m_ulTotalPacked(0)
		, m_ulTotalFiles(0)
		, m_ulMergeSize(0)
	{
	}

	~CUnLzx(void)
	{
		m_GroupList.clear();
		m_EntryList.clear();
	}

	// view a single archive:
	// get archive metadata
	// and list of each entry in the archive
	//
	bool View(tArchiveEntryList &lstArchiveInfo);

	// extract a single archive:
	// give path where files are extracted to,
	// additional directories are created under that (if necessary)
	//
	bool Extract(const std::string &szOutPath);

	// TODO:
	// verify archive integrity
	//
	//bool TestArchive();

	// information about archive file itself
	std::string GetArchiveFileName()
	{
		return m_szArchive;
	}
	size_t GetArchiveFileSize()
	{
		return m_nFileSize;
	}

	// statistical information access to caller
	unsigned long GetTotalSizeUnpacked() 
	{ 
		return m_ulTotalUnpacked; 
	}
	unsigned long GetTotalSizePacked() 
	{ 
		return m_ulTotalPacked; 
	}
	unsigned long GetTotalFileCount() 
	{ 
		return m_ulTotalFiles; 
	}
	unsigned long GetMergeSize() 
	{ 
		return m_ulMergeSize; 
	}
};

#endif // ifndef _UNLZX_H_

