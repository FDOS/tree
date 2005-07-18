/****************************************************************************

  Win32 File compatibility for DOS. 
  [This version does support LFNs, if available.]

  Written by: Kenneth J. Davis
  Date:       August, 2000
  Contact:    jeremyd@computer.org


Copyright (c): Public Domain [United States Definition]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR AUTHORS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

****************************************************************************/


/*** Expects Pointers to be far (Large & compact models ONLY) ***/


#include "w32fDOS.h"
#include <stdlib.h>
#include <string.h>

#define searchAttr ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | \
   FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE )

/* If this variable is nonzero then will 1st attempt LFN findfirst 
 * (findfirst calls sets flag, so findnext/findclose know proper method to continue)
 * else if 0 then only attempt old 0x4E findfirst.
 * This is mostly a debugging tool, may be useful during runtime.
 * Default is LFN_ENABLE.
 */
int LFN_Enable_Flag = LFN_ENABLE;


/* copy old style findfirst data FFDTA to a WIN32_FIND_DATA 
 * NOTE: does not map exactly.  
 * internal to this module only.
 */
static void copyFileData(WIN32_FIND_DATAA *findData, FFDTA *finfo)
{
  /* Copy requried contents over into required structure */
  strcpy(findData->cFileName, finfo->ff_name);
  findData->dwFileAttributes = (DWORD)finfo->ff_attrib;

  /* copy over rest (not quite properly) */
  findData->ftCreationTime.ldw[0] = finfo->ff_ftime;
  findData->ftLastAccessTime.ldw[0] = finfo->ff_ftime;
  findData->ftLastWriteTime.ldw[0] = finfo->ff_ftime;
  findData->ftCreationTime.ldw[1] = finfo->ff_fdate;
  findData->ftLastAccessTime.ldw[1] = finfo->ff_fdate;
  findData->ftLastWriteTime.ldw[1] = finfo->ff_fdate;
  findData->ftCreationTime.hdw = 0;
  findData->ftLastAccessTime.hdw = 0;
  findData->ftLastWriteTime.hdw = 0;
  findData->nFileSizeHigh = 0;
  findData->nFileSizeLow = (DWORD)finfo->ff_fsize;
  findData->dwReserved0 = 0;
  findData->dwReserved1 = 0;
  findData->cAlternateFileName[0] = '\0';
}

HANDLE FindFirstFileA(const char *pathname, WIN32_FIND_DATAA *findData)
{
  char path[1024];
  HANDLE hnd;
  short cflag = 0;  /* used to indicate if findfirst is succesful or not */

  /* verify findData is valid */
  if (findData == NULL)
    return INVALID_HANDLE_VALUE;

  /* allocate memory for the handle */
  if ((hnd = (HANDLE)malloc(sizeof(struct FindFileStruct))) == NULL)
    return INVALID_HANDLE_VALUE;

  /* initialize structure (clear) */
  /* hnd->handle = 0;  hnd->ffdtaptr = NULL; */
  memset(hnd, 0, sizeof(struct FindFileStruct));

  /* Clear findData, this is to fix a glitch under NT, with 'special' $???? files */
  memset(findData, 0, sizeof(struct WIN32_FIND_DATA));

  /* First try DOS LFN (0x714E) findfirst, going to old (0x4E) if error */
  if (LFN_Enable_Flag)
  {
    hnd->flag = FINDFILELFN;

    asm {
      STC                          //; In case not supported
      PUSH SI                      //; Ensure borlands registers unmodified
      PUSH DI
      PUSH DX
      PUSH ES
      PUSH DS
      MOV SI, 1                    //; same format as when old style used, set to 0 for 64bit value
      MOV AX, [WORD PTR findData+2]//; Set address of findData into ES:DI
      MOV ES, AX                   //; 1st Segment
      MOV DI, [WORD PTR findData]  //; now Offset
      MOV AX, [WORD PTR pathname+2]//; Load DS:DX with pointer to path for Findfirst (Segment)
      MOV DS, AX
      MOV DX, [WORD PTR pathname]  //; (Offset)
      MOV AX, 714Eh                //; LFN version of FindFirst
    }
    _CX = searchAttr;
    asm {
      INT 21h                      //; Execute interrupt
      POP DS                       //; Restore registers we modified (except AX)
      POP ES
      POP DX
      POP DI
      POP SI
      JC lfnerror                  //; Test for error or LFN unsupported
    }
    hnd->fhnd.handle = _AX;  /* store handle finally :) */
    return hnd;

lfnerror:
    /* AX is supposed to contain 7100h if function not supported, else real error */
    /* However, FreeDOS returns AX = 1 = Invalid function number instead.         */
    if ((_AX != 0x7100) && (_AX != 0x0001))
    {
      free(hnd);
      return INVALID_HANDLE_VALUE;
    }
  }

  /* Use DOS (0x4E) findfirst, returning if error */
  hnd->flag = FINDFILEOLD;

  /* allocate memory for the FFDTA */
  if ((hnd->fhnd.ffdtaptr = (FFDTA *)malloc(sizeof(struct FFDTA))) == NULL)
  {
    free(hnd);
    return INVALID_HANDLE_VALUE;
  }

  /* if pathname ends in \* convert to \*.* */
  strcpy(path, pathname);
  int eos = strlen(path);
  eos--;
  if (path[eos] == '*')
    if (path[eos-1] == '\\')
      strcat(path, ".*");

  asm {
    PUSH DS                        //; Save Registers borland may care about
    PUSH ES
    MOV AH, 2Fh                    //; Get Current DTA
    INT 21h                        //; Execute interrupt, returned in ES:BX
    PUSH BX                        //; Store its Offset, then Segment
    PUSH ES
    MOV AH, 1Ah                    //; Set Current DTA to our buffer, DS:DX
    LES BX, [hnd]                  //; Load our buffer for new DTA.
    MOV DX, WORD PTR [ES:BX+4]     //; hnd->fhnd.ffdtaptr+2 (+2 for segment)
    MOV DS, DX
    MOV DX, WORD PTR [ES:BX+2]     //; hnd->fhnd.ffdtaptr
    INT 21h                        //; Execute interrupt
    MOV AX, 4E00h                  //; Actual findfirst call
    MOV DX, SS                     //; Load DS:DX with pointer to path for Findfirst
    MOV DS, DX
    LEA DX, path
  }
    _CX = searchAttr;
  asm {
    INT 21h                        //; Execute interrupt
    JNC success                    //; If carry is not set then succesful
    MOV [cflag], AX                //; Set flag with error.
  }
success:
  asm {
    MOV AH, 1Ah               //; Set Current DTA back to original, DS:DX
    POP DS                    //; Popping ES into DS since thats where we need it.
    POP BX                    //; Now DS:BX points to original DTA
    INT 21h                   //; Execute interrupt to restore.
    POP ES                    //; Restore ES and DS
    POP DS
  }

  if (cflag)
  {
    free(hnd->fhnd.ffdtaptr);
    free(hnd);
    return INVALID_HANDLE_VALUE;
  }

  /* copy its results over */
  copyFileData(findData, hnd->fhnd.ffdtaptr);

  return hnd;
}


int FindNextFileA(HANDLE hnd, WIN32_FIND_DATAA *findData)
{
  short cflag = 0;  /* used to indicate if dos findnext succesful or not */

  /* if bad handle given return */
  if ((hnd == NULL) || (hnd == INVALID_HANDLE_VALUE)) return 0;

  /* verify findData is valid */
  if (findData == NULL) return 0;

  /* Clear findData, this is to fix a glitch under NT, with 'special' $???? files */
  memset(findData, 0, sizeof(struct WIN32_FIND_DATA));

  /* Flag indicate if using LFN DOS (0x714F) or not */
  if (hnd->flag == FINDFILELFN)
  {
    _BX = hnd->fhnd.handle;        //; Move the Handle returned by previous findfirst into BX
    asm {
      STC                          //; In case not supported
      PUSH SI                      //; Ensure borlands registers unmodified
      PUSH DI
      PUSH ES
      MOV SI, 1                    //; same format as when old style used, set to 0 for 64bit value
      MOV AX, [WORD PTR findData+2]//; Set address of findData into ES:DI (Segment 1st)
      MOV ES, AX
      MOV DI, [WORD PTR findData]  //; now Offset
      MOV AX, 714Fh                //; LFN version of FindNext
      INT 21h                      //; Execute interrupt
      POP ES                       //; Restore ES
      POP DI                       //; Restore DI
      POP SI                       //; Restore SI
      JC lfnerror
    }
    return 1;   /* success */

lfnerror:
    return 0;   /* Any errors here, no other option but to return error/no more files */
  }
  else  /* Use DOS (0x4F) findnext, returning if error */
  {
    asm {
      PUSH DS                       //; Save Registers borland may care about
      PUSH ES
      MOV AH, 2Fh                   //; Get Current DTA
      INT 21h                       //; Execute interrupt, returned in ES:BX
      PUSH BX                       //; Store its Offset, then Segment
      PUSH ES
      MOV AH, 1Ah                   //; Set Current DTA to our buffer, DS:DX
      LES BX, [hnd]                 //; Load our buffer for new DTA.
      MOV DX, WORD PTR [ES:BX+4]    //; hnd->fhnd.ffdtaptr+2 (+2 for segment)
      MOV DS, DX
      MOV DX, WORD PTR [ES:BX+2]    //; dx = offset(hnd->fhnd.ffdtaptr)
      INT 21h                       //; Execute interrupt
      MOV AX, 4F00h                 //; Actual findnext call
      INT 21h                       //; Execute interrupt
      JNC success                   //; If carry is not set then succesful
      MOV [cflag], AX               //; Set flag with error.
    }
success:
    asm {
      MOV AH, 1Ah                   //; Set Current DTA back to original, DS:DX
      POP DS                        //; Popping ES into DS since thats where we need it.
      POP BX                        //; Now DS:BX points to original DTA
      INT 21h                       //; Execute interrupt to restore.
      POP ES                        //; Restore ES and DS
      POP DS
    }

  if (cflag)
    return 0;

  /* copy its results over */
  copyFileData(findData, hnd->fhnd.ffdtaptr);

  return 1;
  }
}


/* free resources to prevent memory leaks */
void FindClose(HANDLE hnd)
{
  /* 1st check if valid handle given */
  if ((hnd != NULL) && (hnd != INVALID_HANDLE_VALUE))
  {
    /* See if its for the new or old style findfirst */
    if (hnd->flag == FINDFILEOLD) /* Just free memory allocated */
    {
      if (hnd->fhnd.ffdtaptr != NULL)
	free(hnd->fhnd.ffdtaptr);
      hnd->fhnd.ffdtaptr = NULL;
    }
    else /* must call LFN findclose */
    {
      _BX = hnd->fhnd.handle;     /* Move handle returned from findfirst into BX */
      asm {
        STC
        MOV AX, 71A1h
        INT 21h                   /* carry set on error */
      }
      hnd->fhnd.handle = 0;
    }

    free(hnd);                    /* Free memory used for the handle itself */
  }
}



/**
 Try LFN getVolumeInformation 1st
 if successful, assume valid drive/share (ie will return 1 unless error getting label)
 if failed (any error other than unsupported) return 0
 if a drive (ie a hard drive, ram drive, network mapped to letter) [has : as second letter]
 {
   try findfirst for volume label. (The LFN api does not seem to support LABEL attribute searches.)
     If getVolInfo unsupported but get findfirst succeed, assume valid (ie return 1)
   Try Get Serial#
 }
 else a network give \\server\share and LFN getVol unsupported, assume failed 0, as the
   original findfirst/next I haven't seen support UNC naming, clear serial and volume.
*** Currently trying to find a way to get a \\server\share 's serial & volume if LFN available ***
*/

/* Only the 1st 4 arguments are used, returns zero on failure,
 * If lpRootPathName is NULL or "" we use current default drive.
 */
#pragma argsused
int GetVolumeInformation(char *lpRootPathName,char *lpVolumeNameBuffer,
  DWORD nVolumeNameSize, DWORD *lpVolumeSerialNumber,
  DWORD *lpMaximumComponentLength, DWORD *lpFileSystemFlags,
  char *lpFileSystemNameBuffer, DWORD nFileSystemNameSize)
{
  /* File info for getting  Volume Label (using directory entry) */
  FFDTA finfo;

  /* Using DOS interrupt to get serial number */
  struct media_info
  {
    short dummy;
    DWORD serial;
    char volume[11];
    short ftype[8];
  } media;

  /* buffer to store file system name in lfn get volume information */
  char fsystem[32];

  /* Stores the root path we use. */
  char pathname[260];

  /* Used to determine if drive valid (success/failure of this function)
   * 0 = success
   * 1 = failure
   * 2 = LFN api unsupported (tentative failure)
   */
  int cflag=2;

  /* validate root path to obtain info on, NULL or "" means use current */
  if ((lpRootPathName == NULL) || (*lpRootPathName == '\0'))
  {
    /* Assume if NULL user wants current drive, eg C:\ */
    asm {
      MOV AH, 19h             //; Get Current Default Drive
      INT 21h                 //; returned in AL, 0=A, 1=B,...
      PUSH DS                 //; Store DS, then set to stack segment
      MOV BX, SS
      MOV DS, BX
      LEA BX, pathname        //; load pointer to our buffer
      ADD AL, 'A'             //; Convert #returned to a letter
      MOV [BX], AL            //; Store drive letter
      INC BX                  //; point to next character
      MOV BYTE PTR [BX], ':'  //; Store the colon
      INC BX
      MOV BYTE PTR [BX], '\'  //; and the \ (this gets converted correctly as a single \
      INC BX
      MOV BYTE PTR [BX], 0    //; Most importantly the '\0' terminator
      POP DS                  //; Restore dataseg
    }
  }
  else
    strcpy(pathname, lpRootPathName);

  /* Flag indicate if using LFN DOS or not */
  if (LFN_Enable_Flag)
  {
    asm {
      MOV AX, 71A0h            //; LFN GetVolumeInformation
      PUSH DI                  //; Preserve registers for borland
      PUSH DS
      PUSH ES
      MOV DX, SS               //; Load buffer for file system name into ES:DI
      MOV ES, DX
      LEA DI, fsystem
      MOV CX, 32               //; size of ES:DI buffer
      MOV DS, DX
      LEA DX, pathname         //; Load root name into DS:DX
      STC                      //; in case LFN api unsupported
      INT 21h
      POP ES                   //; restore Borland's registers
      POP DS
      POP DI 
      JC getvolerror           //; on any error skip storing any info
    }
    /* store stuff
	    BX = file system flags (see #01783)
	    CX = maximum length of file name [usually 255]
	    DX = maximum length of path [usually 260]
	    fsystem buffer filled (ASCIZ, e.g. "FAT","NTFS","CDFS")
    */
    cflag = 0;                 //; indicate no error
    goto endgetvol;

getvolerror:
    asm {
      CMP AX, 7100h            //; see if real error or unsupported
      JE endgetvol             //; if so skip ahead
      CMP AX, 0001h            //; FreeDOS returns AX = 1 = Invalid function number
      JE endgetvol
    }
    cflag = 1;                 //; indicate failure

endgetvol:
  }


  if (cflag != 1)  /* if no error validating volume info or LFN getVolInfo unsupported */
  {
    /* if a drive, test if valid, get volume, and possibly serial # */
    if (pathname[1] == ':')
    {
      /* assume these calls will succeed, change on an error */
      cflag = 0;

      /* get path ending in \*.*, */
      if (pathname[strlen(pathname)-1] != '\\')
        strcat(pathname, "\\*.*");
      else
        strcat(pathname, "*.*");

      /* Search for volume using old findfirst, as LFN version (NT5 DOS box) does
       * not recognize FILE_ATTRIBUTE_LABEL = 0x0008 as label attribute.
       */
      asm {
        PUSH DS                   //; Store registers borland may care about
        PUSH ES
        MOV AH, 2Fh               //; Get Current DTA
        INT 21h                   //; Execute interrupt, returned in ES:BX
        PUSH BX                   //; Store its Offset, then Segment
	PUSH ES
	MOV AH, 1Ah               //; Set Current DTA to our buffer, DS:DX
	MOV DX, SS
	MOV DS, DX
	LEA DX, finfo       	    //; Load our buffer for new DTA.
	INT 21h                   //; Execute interrupt
	MOV AX, 4E00h             //; Actual findfirst call
	LEA DX, pathname          //; Load DS:DX with pointer to modified RootPath for Findfirt
	MOV CX, FILE_ATTRIBUTE_LABEL
	INT 21h                   //; Execute interrupt, Carry set on error, unset on success
	JNC success               //; If carry is not set then succesful
	MOV [cflag], AX           //; Set flag with error.
	JMP cleanup               //; Restore DTA
      }
success:                          //; True volume entry only has volume attribute set [MS' LFNspec]
      asm {                       //;     But they may also have archive bit set!!!
	LEA BX, finfo.ff_attrib   //; Load address of file's attributes returned by findfirst/next
	MOV AL, BYTE PTR [BX]     //; Looking for a BYTE that is FILE_ATTRIBUTE_LABEL only
	AND AL, 0xDF              //; Ignore Archive bit
	CMP AL, FILE_ATTRIBUTE_LABEL
	JE cleanup                //; They match, so should be true volume entry.
	MOV AX, 4F00h             //; Otherwise keep looking (findnext)
	INT 21h                   //; Execute interrupt
	JNC success               //; If carry is not set then succesful
	MOV [cflag], AX           //; Set flag with error.
      }
cleanup:
      asm {
	MOV AH, 1Ah               //; Set Current DTA back to original, DS:DX
	POP DS                    //; Popping ES into DS since thats where we need it.
	POP BX                    //; Now DS:BX points to original DTA
	INT 21h                   //; Execute interrupt to restore.
	POP ES                    //; Restore borland's registers
	POP DS
      }
      /* copy over volume label, if buffer given */
      if (lpVolumeNameBuffer != NULL)
      {
	if (cflag != 0)    /* error or no label */
	  lpVolumeNameBuffer[0] = '\0';
	else                        /* copy up to buffer's size of label */
	{
	  strncpy(lpVolumeNameBuffer, finfo.ff_name, nVolumeNameSize);
	  lpVolumeNameBuffer[nVolumeNameSize-1] = '\0';
	  /* slide characters over if longer than 8 to remove . */
	  if (lpVolumeNameBuffer[8] == '.')
	  {
	    lpVolumeNameBuffer[8] = lpVolumeNameBuffer[9];
	    lpVolumeNameBuffer[9] = lpVolumeNameBuffer[10];
	    lpVolumeNameBuffer[10] = lpVolumeNameBuffer[11];
	    lpVolumeNameBuffer[11] = '\0';
	  }
	}
      }
      /* Test for no label found, which is not an error,
	 Note: added the check for 0x02 as FreeDOS returns this instead
	 at least for disks with LFN entries and no volume label.
      */
      if ((cflag == 0x12) || /* No more files or   */
	  (cflag == 0x02))   /* File not found     */
	cflag = 0;       /* so assume valid drive  */


      /* Get Serial Number, only supports drives mapped to letters */
      media.serial = 0;         /* set to 0, stays 0 on an error */

      _BX = (pathname[0] & '\xDF') - 'A' + 1; /* Clear BH, drive in BL */
      asm {
        PUSH DS                 //; Store DS and set DS=SS
        MOV DX, SS
        MOV DS, DX
        LEA DX, media           //; DS:DX pointer to media structure
        MOV CX, 0866h           //; CH=disk drive, CL=Get Serial #
        MOV AX, 440Dh           //; Generic IOCTL
        INT 21h
        POP DS                  //; Restore DS
      }

/***************** Replaced with 'documented' version of Get Serial Number *********************/
      /* NT2000pro does NOT set or clear the carry for int21h subfunction 6900h
       *   if an error occurs, it leaves media unchanged.
       */
//      asm {
//        MOV AX, 0x6900
//        INT 21h                 //; Should set carry on error, clear on success [note NT5 does not]
//      }
/***************** End with 'undocumented' version of Get Serial Number *********************/

      if (lpVolumeSerialNumber != NULL)
	*lpVolumeSerialNumber = media.serial;
    }
    else /* a network drive, assume results of LFN getVolInfo, no volume or serial [for now] */
    {
      if (lpVolumeNameBuffer != NULL)
	lpVolumeNameBuffer[0] = '\0';

      if (lpVolumeSerialNumber != NULL)
	*lpVolumeSerialNumber = 0x0;
    }
  }

  /* If there was an error getting the validating drive return failure) */
  if (cflag)    /* cflag is nonzero on any errors */
    return 0;   /* zero means error! */
  else
    return 1;   /* Success (drive exists we think anyway) */
}

