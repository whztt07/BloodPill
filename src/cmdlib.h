// cmdlib.h

#ifndef __CMDLIB__
#define __CMDLIB__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
//#include <unistd.h>

#ifndef __BYTEBOOL__
#define __BYTEBOOL__
typedef enum {false, true} qboolean;
typedef unsigned char byte;
#endif

// the dec offsetof macro doesn't work very well...
#define myoffsetof(type,identifier) ((size_t)&((type *)0)->identifier)

// LordHavoc: increased maximum token length from 128 to 16384
#define	MAXTOKEN	16384

// VorteX: avoid 'deprecated' messages
#if _MSC_VER >= 1400
#define stricmp _stricmp
#define strnicmp _strnicmp
#define mkdir _mkdir
#endif

// lists, used for several switches
#define MAX_LIST_ITEMS	256
typedef struct list_s
{
	int			items;
	char		*item[MAX_LIST_ITEMS];
	unsigned char x[MAX_LIST_ITEMS];
}list_t;
list_t *NewList();
void FreeList(list_t *list);
void ListAdd(list_t *list, const char *str, unsigned char x);

// set these before calling CheckParm
extern int myargc;
extern char **myargv;

extern char *Q_strupr (char *in);
extern char *Q_strlower (char *in);
extern int Q_strncasecmp (char *s1, char *s2, int n);
extern int Q_strcasecmp (char *s1, char *s2);
extern char *ConvSlashU2W (char *start);
extern char *ConvSlashW2U (char *start);
//extern void Q_getwd (char *out);
size_t strlcat(char *dst, const char *src, size_t siz);
size_t strlcpy(char *dst, const char *src, size_t siz);

extern int Q_filelength (FILE *f);
extern int	FileTime (char *path);

extern void	Q_mkdir (char *path);

void CreatePath (char *path);

extern double I_DoubleTime (void);

extern void Error (char *error, ...);

extern int CheckParm (char *check);

extern FILE *SafeOpen (char *filename, char mode[]);
extern FILE *SafeOpenWrite (char *filename);
extern void SafeRead (FILE *f, void *buffer, int count);
extern void SafeWrite (FILE *f, void *buffer, int count);

extern int LoadFile (char *filename, void **bufferptr);
extern void SaveFile (char *filename, void *buffer, int count);

extern void DefaultPath (char *path, char *basepath);
extern void ReplaceExtension (char *path, char *oldextension, char *replacementextension, char *missingextension);
void DefaultExtension (char *path, const char *extension, size_t size_path);

extern void ExtractFilePath (char *path, char *dest);
extern void ExtractFileBase (char *path, char *dest);
extern void StripFileExtension (char *path, char *dest);
extern void ExtractFileExtension (char *path, char *dest);
void AddSuffix(char *outpath, char *inpath, char *suffix);
extern qboolean FileExists(char *filename);

extern int ParseNum (char *str);
int ParseHex(char *hex);

extern char *COM_Parse (char *data);

extern char com_token[MAXTOKEN];
extern qboolean com_eof;

extern char *copystring(char *s);

extern char token[MAXTOKEN];
extern int	scriptline;

unsigned int ReadUInt(byte *buffer);
unsigned int ReadUShort(byte *buffer);
int ReadShort(byte *buffer);

void StartTokenParsing (char *data);
qboolean GetToken (qboolean crossline);
void UngetToken (void);

extern void CRC_Init(unsigned short *crcvalue);
extern void CRC_ProcessByte(unsigned short *crcvalue, byte data);
extern unsigned short CRC_Value(unsigned short crcvalue);

extern void COM_CreatePath (char *path);
extern void COM_CopyFile (char *from, char *to);

extern	char	filename_map[1024];
extern	char	filename_bsp[1024];
extern	char	filename_prt[1024];
extern	char	filename_pts[1024];
extern	char	filename_lit[1024];
extern	char	filename_dlit[1024];
extern	char	filename_lights[1024];

#endif
