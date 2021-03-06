/* 
	PSPRadio / Music streaming client for the PSP. (Initial Release: Sept. 2005)
	Copyright (C) 2005  Rafael Cabezas a.k.a. Raf
	
	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>

#include <PSPApp.h>
#include <PSPSound.h>
#include <pspwlan.h> 
#include <psphprm.h>
#include <assert.h>

#include <iniparser.h>
#include <Logging.h>
#include <UI_Interface.h>
#include "SHOUTcastScreen.h"
#include <Main.h>

#define SHOUTCAST_DB_REQUEST_STRING				"http://www.shoutcast.com/sbin/xmllister.phtml?service=pspradio&no_compress=1"
#define SHOUTCAST_DB_COMPRESSED_REQUEST_STRING 	"http://www.shoutcast.com/sbin/xmllister.phtml?service=pspradio"
#define SHOUTCAST_DB_COMPRESSED_FILENAME		"SHOUTcast/db.xml.gz"
#define SHOUTCAST_DB_FILENAME					"SHOUTcast/db.xml"
#define SHOUTCAST_DB_FILENAME_BACKUP			"SHOUTcast/db.xml.bk"

/** New */
#define DB_NEW_REQUEST_STRING					"http://www.shoutcast.com/sbin/newxml.phtml?genre=Top500"
#define DB_NEW_FILENAME							"SHOUTcast/newdb.xml"

SHOUTcastScreen::SHOUTcastScreen(int Id, CScreenHandler *ScreenHandler)
	:PlayListScreen(Id, ScreenHandler)
{
	Log(LOG_VERYLOW,"SHOUTcastScreen Ctor.");
	m_Lists = NULL;
}


SHOUTcastScreen::~SHOUTcastScreen()
{

}

int SHOUTcastScreen::LoadLists()
{
	int iElementNo = 0;

	if (!m_Lists)
	{
		m_Lists = new CMetaDataContainer();
	}

	if (m_Lists)
	{
		Log(LOG_LOWLEVEL, "StartScreen::PSPRADIO_SCREEN_SHOUTCAST_BROWSER");
		m_Lists->Clear();
		
		//Log(LOG_LOWLEVEL, "Loading xml file '%s'.", SHOUTCAST_DB_FILENAME);
		//iElementNo = m_Lists->LoadSHOUTcastXML(SHOUTCAST_DB_FILENAME);
		Log(LOG_LOWLEVEL, "Loading xml file '%s'.", DB_NEW_FILENAME);
		iElementNo = m_Lists->LoadNewSHOUTcastXML(DB_NEW_FILENAME);

		m_Lists->SetCurrentSide(CMetaDataContainer::CONTAINER_SIDE_CONTAINERS);
	}

	return iElementNo;
}

bool UnCompress(char *strSourceFile, char *strDestFile);
int inf(FILE *source, FILE *dest);

bool CScreenHandler::DownloadSHOUTcastDB()
{
	bool success = false;
	bool isCompressedDownload = true;
	CPSPStream *WebConnection = new CPSPStream();
	char *strRequestString = SHOUTCAST_DB_COMPRESSED_REQUEST_STRING;

	/** First, we back-up the db in case the download fails.. */
	int iRet = rename(SHOUTCAST_DB_FILENAME, SHOUTCAST_DB_FILENAME_BACKUP);

	strRequestString = strdup(gPSPRadio->GetConfig()->GetString("SHOUTCAST:DB_REQUEST_STRING", 
							  SHOUTCAST_DB_COMPRESSED_REQUEST_STRING));
	
	if (strstr(strRequestString, "no_compress=1"))
	{
		isCompressedDownload = false;
	}
	else
	{
		isCompressedDownload = true;
	}
	
	WebConnection->SetURI(strRequestString);
	WebConnection->Open();
	if (true == WebConnection->IsOpen())
	{
		Log(LOG_INFO, "DownloadSHOUTcastDB(): Connected - Downloading '%s'", 
			isCompressedDownload?SHOUTCAST_DB_COMPRESSED_FILENAME:SHOUTCAST_DB_FILENAME);
		bool bRet;
		size_t bytes;
		bRet = WebConnection->DownloadToFile((char*)(isCompressedDownload?SHOUTCAST_DB_COMPRESSED_FILENAME:SHOUTCAST_DB_FILENAME),
											 bytes);
		
		if (true == bRet)
		{
			WebConnection->Close();
			delete WebConnection, WebConnection = NULL;
			Log(LOG_INFO, "DownloadSHOUTcastDB(): DB Retrieved. (%dbytes)", bytes);
			if (isCompressedDownload == true)
			{
				Log(LOG_INFO, "Uncompressing.");
				gPSPRadio->m_UI->DisplayMessage("Uncompressing . . .");
				bRet = UnCompress(SHOUTCAST_DB_COMPRESSED_FILENAME, SHOUTCAST_DB_FILENAME);
				if (true == bRet)
				{
					gPSPRadio->m_UI->DisplayMessage("SHOUTcast DataBase Retrieved");
					Log(LOG_INFO, "SHOUTcast.com DB retrieved.");
					success = true;
				}
				else
				{
					Log(LOG_ERROR, "Error uncompressing '%s' to '%s'",SHOUTCAST_DB_COMPRESSED_FILENAME, SHOUTCAST_DB_FILENAME);
					gPSPRadio->m_UI->DisplayMessage("Error Uncompressing...");
				}
			}
			else
			{
				if (bytes > 0)
				{
					success = true;
				}
			}
		}
	}
	else
	{
		Log(LOG_ERROR, "Error connecting to '%s'", strRequestString);
		gPSPRadio->m_UI->DisplayErrorMessage("Can't connect to SHOUTcast.com");
	}
	
	/** The compressed file is not needed anymore */
	if (isCompressedDownload)
	{
		remove (SHOUTCAST_DB_COMPRESSED_FILENAME);
	}

	if (false == success)
	{
		/** We restore the back-up of the db, as the download failed.. */
		Log(LOG_ERROR, "Error, so restoring from backup '%s'", SHOUTCAST_DB_FILENAME_BACKUP);
		iRet = rename(SHOUTCAST_DB_FILENAME_BACKUP, SHOUTCAST_DB_FILENAME);

	}
	else
	{
		Log(LOG_LOWLEVEL, "Success, so removing backup '%s'", SHOUTCAST_DB_FILENAME_BACKUP);
		remove(SHOUTCAST_DB_FILENAME_BACKUP);
	}

	return success;
}

bool CScreenHandler::DownloadNewSHOUTcastDB()
{
	bool success = false;
	CPSPStream *WebConnection = new CPSPStream();
	char *strRequestString = DB_NEW_REQUEST_STRING;

	/** First, we back-up the db in case the download fails.. */
	int iRet = rename(DB_NEW_FILENAME, SHOUTCAST_DB_FILENAME_BACKUP);

	strRequestString = strdup(gPSPRadio->GetConfig()->GetString("SHOUTCAST:DB_NEW_REQUEST_STRING", 
							  DB_NEW_REQUEST_STRING));
	
	WebConnection->SetURI(strRequestString);
	WebConnection->Open();
	if (true == WebConnection->IsOpen())
	{
		bool bRet;
		size_t bytes;

		Log(LOG_INFO, "DownloadNewSHOUTcastDB(): Connected - Downloading '%s'", 
			DB_NEW_FILENAME);
		bRet = WebConnection->DownloadToFile((char*)(DB_NEW_FILENAME), bytes);
		
		if (true == bRet)
		{
			WebConnection->Close();
			delete WebConnection, WebConnection = NULL;
			Log(LOG_INFO, "DownloadNewSHOUTcastDB(): DB Retrieved. (%dbytes)", bytes);
			if (bytes > 0)
			{
				success = true;
			}
		}
	}
	else
	{
		Log(LOG_ERROR, "Error connecting to '%s'", strRequestString);
		gPSPRadio->m_UI->DisplayErrorMessage("Can't connect to SHOUTcast.com");
	}
	
	if (false == success)
	{
		/** We restore the back-up of the db, as the download failed.. */
		Log(LOG_ERROR, "Error, so restoring from backup '%s'", SHOUTCAST_DB_FILENAME_BACKUP);
		iRet = rename(SHOUTCAST_DB_FILENAME_BACKUP, DB_NEW_FILENAME);
	}
	else
	{
		Log(LOG_LOWLEVEL, "Success, so removing backup '%s'", SHOUTCAST_DB_FILENAME_BACKUP);
		remove(SHOUTCAST_DB_FILENAME_BACKUP);
	}

	return success;
}

/** Raf: Thanks to echto for the uncompression routines! */
#include "zlib.h"

#define CHUNK 16384

bool UnCompress(char *strSourceFile, char *strDestFile)
{
	bool success = false;
	FILE *infile  = NULL;
	FILE *outfile = NULL;
		
	srand(sceKernelLibcTime(NULL)); // seeded with time

	infile  = fopen(strSourceFile,"r");
	
	if (infile)
	{
		outfile = fopen(strDestFile,"w");
		if (outfile)
		{
			/* do decompression */
			int ret = inf(infile, outfile);
			if (ret == Z_OK)
			{
				Log(LOG_LOWLEVEL, "UnCompress(): File '%s' decompressed successfully", strSourceFile);
				success = true;
			}
			else
			{
				/* report a zlib or i/o error */
				switch (ret) 
				{
					case Z_ERRNO:
						if (ferror(stdin))
							Log(LOG_ERROR, "UnCompress(): zlib error: error reading file");
						if (ferror(stdout))
							Log(LOG_ERROR, "UnCompress(): zlib error: error writing file");
						break;
					case Z_STREAM_ERROR:
						Log(LOG_ERROR, "UnCompress(): zlib error: invalid compression level");
						break;
					case Z_DATA_ERROR:
						Log(LOG_ERROR, "UnCompress(): zlib error: invalid or incomplete deflate data");
						break;
					case Z_MEM_ERROR:
						Log(LOG_ERROR, "UnCompress(): zlib error: out of memory");
						break;
					case Z_VERSION_ERROR:
						Log(LOG_ERROR, "UnCompress(): zlib error: zlib version mismatch!");
				}
				success = false;
			}
			fclose(outfile);
		}
		else
		{
			Log(LOG_ERROR, "UnCompress(): Unable to open file for writing '%s'", strDestFile);
		}
		fclose(infile);
	}
	else
	{
		Log(LOG_ERROR, "UnCompress(): Unable to open file for reading '%s'", strSourceFile);
	}

	return success;
}

int inf(FILE *source, FILE *dest)
{
    int ret;
    unsigned have;
    z_stream strm;
    Bytef in[CHUNK];
    Bytef out[CHUNK];

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)inflateEnd(&strm);
            return Z_ERRNO;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
            }
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

