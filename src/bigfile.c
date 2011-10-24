////////////////////////////////////////////////////////////////
//
// Bigfile operations
// coded by Pavel [VorteX] Timofeyev and placed to public domain
// thanks to XentaX (www.xentax.com) community for providing bigfile specs
//
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
////////////////////////////////


#include "bloodpill.h"
#include "bigfile.h"
#include "soxsupp.h"
#include "cmdlib.h"
#include "mem.h"
#include "BO1.h"
#include <windows.h>

#define DEFAULT_BIGFILENAME	"pill.big"
#define DEFAULT_PACKPATH	"bigfile"

char bigfile[MAX_BLOODPATH];
bigklist_t *bigklist = NULL;

/*
==========================================================================================

  UTIL FUNCTIONS

==========================================================================================
*/

bigentrytype_t ParseBigentryTypeFromExtension(char *ext)
{
	int i, numbytes;

	Q_strlower(ext);
	numbytes = strlen(ext);
	// null-length ext?
	if (!numbytes)
		return BIGENTRY_UNKNOWN;
	// find type
	for (i = 0; i < BIGFILE_NUM_FILETYPES; i++)
		if (!memcmp(bigentryext[i], ext, numbytes))
			return i;
	return BIGENTRY_UNKNOWN;
}

bigentrytype_t ParseBigentryType(char *str)
{
	bigentrytype_t bigtype;

	// file type by ext
	bigtype = ParseBigentryTypeFromExtension(str);
	if (bigtype != BIGENTRY_UNKNOWN)
		return bigtype;

	// special keywords
	if (!strcmp(str, "raw"))
		return BIGENTRY_SPRITE;
	if (!strcmp(str, "unknown") || !strcmp(str, "data"))
		return BIGENTRY_UNKNOWN;
	if (!strcmp(str, "wave") || !strcmp(str, "riff") || !strcmp(str, "riffwave"))
		return BIGENTRY_RIFF_WAVE;
	return BIGENTRY_UNKNOWN;
}

char *UnparseBigentryType(bigentrytype_t bigtype)
{
	return bigentryext[bigtype];
}

/*
==========================================================================================

  CHECK IF BIGFILE ENTRY MATCHES LIST

==========================================================================================
*/

// #hash
// $type
// wildcard - *
// whether to match include-exclude list
qboolean MatchIXList(bigfileentry_t *entry, list_t *list, qboolean matchtypes, qboolean matchnames)
{
	char *buf, *name;
	unsigned int hash;
	int skip;
	int i;

	for (i = 0; i < list->items; i++)
	{
		buf = list->item[i];
		// hashname compare
		if (buf[0] == '#')
		{
			buf++;
			sscanf(buf, "%X", &hash);
			if (entry->hash == hash)
				return (list->x[i]) ? true : false;
			continue;
		}
		// type compare
		if (buf[0] == '$')
		{
			if (matchtypes)
			{
				buf++;
				if (entry->type == ParseBigentryType(buf))
					return (list->x[i]) ? true : false;
			}
			continue;
		}
		// wildcard compare
		if (!matchnames)
			continue;
	
		skip = false;
		name = entry->name;
		while(name)
		{
			if (buf[0] == '*')
			{
				skip = -1;
				buf++; if (!buf[0]) break;
			}
			else if (buf[0] == '?')
			{
				skip = 1;
				buf++; if (!buf[0]) break;
			}
			// * were in previous try, skip all symbols until find match
			if (skip)
			{
				if (skip != -1) // one char skip
					name++;
				else 
				{
					while(name[0] && name[0] != buf[0])
						name++;
				}
				if (!name[0]) break;
				name++;
				buf++; if (!buf[0]) break;
				skip = 0;
				continue;
			}
			// check a char and continue
			if (name[0] != buf[0]) break;
			name++;
			buf++; if (!buf[0]) break;
		}
		if (!name[0] || skip == -1) // passed
			return (list->x[i]) ? true : false;
	}
	return false;
}

/*
==========================================================================================

  KNOWN-FILES FEATURE

==========================================================================================
*/

bigklist_t *BigfileLoadKList(char *filename, qboolean stopOnError)
{
	bigklist_t *klist;
	bigkentry_t *entry;
	int linenum = 0;
	char line[1024], temp[256], ext[15];
	unsigned int hash;
	qboolean options;
	int val, i;
	FILE *f;

	// create empty klist
	klist = qmalloc(sizeof(bigklist_t));
	klist->entries = NULL;
	klist->numentries = 0;

	if (stopOnError)
		f = SafeOpen(filename, "r");
	else
	{
		f = fopen(filename, "r");
		if (!f)
			return klist;
	}

	// first pass - scan klist to determine how many strings we should allocate
	while(!feof(f))
	{
		fgets(line, 1024, f);
		if (line[0] == '[')
			linenum++;
	}

	// allocate
	klist->entries = qmalloc(linenum * sizeof(bigkentry_t));

	// seconds pass - parse klist
	fseek(f, 0, SEEK_SET);
	linenum = 0;
	while(!feof(f))
	{
		linenum++;
		fgets(line, 1024, f);

		// new entry
		if (line[0] == '[')
		{
			if (!strcmp(line, "[options]"))
				options = true;
			else
			{
				options = false;
				if (sscanf(line, "[%X]", &hash) < 1)
					Error("bad entry definition on line %i: %s", linenum, line);

				// new entry
				entry = &klist->entries[klist->numentries];
				entry->hash = hash;
				entry->adpcmrate = 11025; // default ADPCM sampling rate
				entry->type = BIGENTRY_UNKNOWN;
				entry->rawinfo = NULL;
				entry->pathonly = false;
				strcpy(entry->path, "");
				
				// warn for double defienition
				for (i = 0; i < klist->numentries; i++)
					if (klist->entries[i].hash == hash)
						Warning("redefenition of hash %.8X on line %i", hash, linenum);

				klist->numentries++;
			}
			continue;
		}

		// option directives
		if (options == true)
		{
			continue;
		}

		if (entry == NULL)
			continue;

		// parms
		if (sscanf(line, "type=%s", &temp))
		{
			entry->type = ParseBigentryType(temp);
			if (entry->type == BIGENTRY_SPRITE)
				entry->rawinfo = NewRawInfo();
			continue;
		}
		if (entry->type == BIGENTRY_SPRITE)
		{
			if (ReadRawInfo(line, entry->rawinfo) == true)
				continue;
		}
	
		// path=%s - force file path
		// if file with extension (eg sound/kain1.vag) - a full path override
		// otherwise just add path to hashed name
		if (sscanf(line,"path=%s", &temp))
		{
			ExtractFileExtension(temp, ext);
			// no extension, so this is a path
			entry->pathonly = ext[0] ? false : true;
			strcpy(entry->path, temp);
			// warn for double path definition
			if (ext[0])
				for (i = 0; i < (klist->numentries - 1); i++)
					if (!strcmp(klist->entries[i].path, entry->path))
						Warning("path '%s' redefenition on entry #%.8X on line %i (previously defined for entry #%.8X\n", entry->path, entry->hash, linenum, klist->entries[i].hash);
			continue;
		}

		// rate=%i - force ADPCM rate (conversion needs to know)
		if (sscanf(line,"rate=%i", &val))
		{
			entry->adpcmrate = val;
			continue;
		}

		// warn for bad syntax
		if (line[0] == '\n')
			continue;
		if (line[0] == '#')
			continue;
		Warning("bad line %i: %s", linenum, line);
	}

	Verbose("%s: %i entries\n", filename, klist->numentries);
	return klist;
}

bigkentry_t *BigfileSearchKList(unsigned int hash)
{
	int i;

	if (bigklist)
	{
		for (i = 0; i < bigklist->numentries; i++)
			if (bigklist->entries[i].hash == hash)
				return &bigklist->entries[i];
	}
	return NULL;
}

/*
==========================================================================================

  BigFile subs

==========================================================================================
*/

void BigfileEmptyEntry(bigfileentry_t *entry)
{
	entry->data = NULL;
	entry->adpcmrate = 11025;
	entry->rawinfo = NULL;
}

void BigfileSeekFile(FILE *f, bigfileentry_t *entry)
{
	if (fseek(f, (long int)entry->offset, SEEK_SET))
		Error( "error seeking for data on file %.8X", entry->hash);
}

void BigfileSeekContents(FILE *f, byte *contents, bigfileentry_t *entry)
{
	if (fseek(f, (long int)entry->offset, SEEK_SET))
		Error( "error seeking for data on file %.8X", entry->hash);

	if (fread(contents, entry->size, 1, f) < 1)
		Error( "error reading data on file %.8X (%s)", entry->hash, strerror(errno));
}

void BigfileWriteListfile(FILE *f, bigfileheader_t *data)
{
	bigfileentry_t *entry;
	int i, k;

	if (f != stdout)
		fprintf(f, "numentries=%i\n", data->numentries);
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];

		// write general data
		fprintf(f, "\n", entry->hash);
		fprintf(f, "# entry %i\n", i + 1);
		fprintf(f, "[%.8X]\n", entry->hash);
		fprintf(f, "type=%i\n", (int)entry->type);
		fprintf(f, "size=%i\n", (int)entry->size);
		fprintf(f, "offset=%i\n", (int)entry->offset);
		fprintf(f, "file=%s\n", entry->name);

		// write specific data for TIM images
		switch(entry->type)
		{
			case BIGENTRY_TIM:
				fprintf(f, "tim.layers=%i\n", entry->timlayers);
				for(k = 0; k < entry->timlayers; k++)
				{
					fprintf(f, "tim[%i].type=%i\n", k, entry->timtype[k]);
					fprintf(f, "tim[%i].xskip=%i\n", k, entry->timxpos[k]);
					fprintf(f, "tim[%i].yskip=%i\n", k, entry->timypos[k]);
				}
				break;
			case BIGENTRY_RAW_ADPCM:
				fprintf(f, "adpcm.rate=%i\n", entry->adpcmrate);
				break;
			case BIGENTRY_SPRITE:
				WriteRawInfo(f, entry->rawinfo);
				break;
			default:
				break;
		}
	}
}

// retrieves entry hash from name
unsigned int BigfileEntryHashFromString(char *string, qboolean casterror)
{
	unsigned int hash;
	int i;

	if (string[0] == '#')
	{
		sscanf(string, "#%X", &hash);
		return hash;
	}
	// filename or path
	for (i = 0; i < NUM_CSV_ENTRIES; i++)
		if (!stricmp(string, wheelofdoom_names[i].name))
			if (strlen(string) == strlen(wheelofdoom_names[i].name))
				break;
	if (i < NUM_CSV_ENTRIES)
	{
		hash = wheelofdoom_names[i].hash;
		// Verbose("Hash filename: %.8X\n", hash);
		return hash;
	}
	if (casterror)
		Error("BigfileEntryHashFromString: Failed to lookup entry name '%s' - no such entry\n", string);
	return 0;
}

// finds entry by hash
bigfileentry_t *BigfileGetEntry(bigfileheader_t *bigfile, unsigned int hash)
{
	int i;

	for (i = 0; i < (int)bigfile->numentries; i++)
		if (bigfile->entries[i].hash == hash)
			return &bigfile->entries[i];
	return NULL;
}

// quick way to get entry data from header
bigfileentry_t *ReadBigfileHeaderOneEntry(FILE *f, unsigned int hash)
{
	unsigned int numentries, i, *read;
	bigfileentry_t *entry;

	entry = NULL;
	if (fread(&numentries, sizeof(unsigned int), 1, f) < 1)
		Error("BigfileHeader: wrong of broken file\n");
	if (!numentries)
		Error("BigfileHeader: funny entries count, perhaps file is broken\n");

	read = qmalloc(numentries * 3 * sizeof(unsigned int));
	if (fread(read, numentries * 3 * sizeof(unsigned int), 1, f) < 1)
		Error("BigfileHeader: error reading header %s\n", strerror(errno));
	
	for (i = 0; i < numentries*3; i += 3)
	{
		if (read[i] != hash)
			continue;
		// make entry
		entry = qmalloc(sizeof(bigfileentry_t));
		BigfileEmptyEntry(entry);
		
		entry->hash = read[i];
		entry->size = read[i+1];
		entry->offset = read[i+2];
		entry->type = BIGENTRY_UNKNOWN;
		// assign default name
		sprintf(entry->name, "%s%.8X.%s", bigentryautopaths[BIGENTRY_UNKNOWN], read[0], bigentryext[BIGENTRY_UNKNOWN]);
		if (!entry->hash || !entry->offset)
			Error("BigfileHeader: entry %i is broken\n", i);
		// assign known name
		for (i = 0; i < NUM_CSV_ENTRIES; i++)
		{
			if (wheelofdoom_names[i].hash == entry->hash)
			{
				sprintf(entry->name, "%s%s", bigentryautopaths[BIGENTRY_UNKNOWN], wheelofdoom_names[i].name);
				break;
			}
		}
		break;
	}

	qfree(read);
	return entry;
}

bigfileheader_t *ReadBigfileHeader(FILE *f, qboolean loadfilecontents, qboolean hashnamesonly)
{	
	bigfileheader_t *data;
	bigfileentry_t *entry;
	FILE *csvf;
	char line[512], temp[256];
	unsigned int read[3];
	unsigned int hash;
	int i, linenum, namesloaded;

	data = qmalloc(sizeof(bigfileheader_t));

	// read header
	fseek(f, SEEK_SET, 0);
	if (fread(&data->numentries, sizeof(unsigned int), 1, f) < 1)
		Error("BigfileHeader: wrong of broken file\n");
	if (!data->numentries)
		Error("BigfileHeader: funny entries count, perhaps file is broken\n");
	Verbose("%i entries in bigfile", data->numentries);

	// read entries
	data->entries = qmalloc(data->numentries * sizeof(bigfileentry_t));
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		BigfileEmptyEntry(entry);

		Pacifier("reading entry %i of %i...", i + 1, data->numentries);

		if (fread(&read, 12, 1, f) < 1)
			Error("BigfileHeader: error on entry %i (%s)\n", i, strerror(errno));
		entry->hash = read[0];
		entry->size = read[1];
		entry->offset = read[2];
		entry->type = BIGENTRY_UNKNOWN;
		// assign default name
		sprintf(entry->name, "%s%.8X.%s", bigentryautopaths[BIGENTRY_UNKNOWN], read[0], bigentryext[BIGENTRY_UNKNOWN]);
		if (!entry->hash || !entry->offset)
			Error("BigfileHeader: entry %i is broken\n", i);
	}
	PacifierEnd();

	// load CSV list for filenames
	if (!hashnamesonly)
	{
		if (FileExists("BO1.csv"))
		{
			csvf = SafeOpen("BO1.csv", "r");
			linenum = 0;
			namesloaded = 0;
			while(!feof(csvf))
			{
				linenum++;
				fgets(line, 512, csvf);
				if (!sscanf(line, "%i;%s", &hash, temp))
				{
					Verbose("Warning: corrupted line %i in BO1.csv: '%s'!\n", linenum, line);
					continue;
				}
				// find hash
				for (i = 0; i < (int)data->numentries; i++)
				{
					if (data->entries[i].hash != hash)
						continue;
					ConvSlashW2U(temp);
					ExtractFileName(temp, line); 
					sprintf(entry->name, "%s%s", bigentryautopaths[BIGENTRY_UNKNOWN], line);
					namesloaded++;
					break;
				}
			}
			Verbose("BO1.csv: loaded %i names\n", namesloaded);
			fclose(csvf);
			// write BO1.h
			csvf = SafeOpenWrite("BO1.h");
			fprintf(csvf, "// BO1.h, converted automatically from Rackot's BO1.csv\n");
			fprintf(csvf, "// Thanks to Ben Lincoln And Andrey [Rackot] Grachev for this\n");
			fprintf(csvf, "// do not modify\n");
			fprintf(csvf, "typedef struct\n");
			fprintf(csvf, "{\n");
			fprintf(csvf, "	unsigned int	hash;\n");
			fprintf(csvf, "	char	name[17];\n");
			fprintf(csvf, "}wheelofdoom_names_t;\n");
			fprintf(csvf, "\n");
			fprintf(csvf, "#define NUM_CSV_ENTRIES %i\n", linenum);
			fprintf(csvf, "\n");
			fprintf(csvf, "wheelofdoom_names_t wheelofdoom_names[NUM_CSV_ENTRIES] =\n");
			fprintf(csvf, "{\n");
			for (i = 0; i < (int)data->numentries; i++)
			{
				ExtractFileName(data->entries[i].name, temp);
				if (strlen(temp) > 16)
				{
					Verbose("Warning: BO1.h: name '%s' is more that 16 chars, will be truncated!\n", temp);
					temp[17] = 0;
				}
				if (i+1 < (int)data->numentries)
					fprintf(csvf, "	{%10i, \"%s\"},\n", data->entries[i].hash, temp);
				else
					fprintf(csvf, "	{%10i, \"%s\"}\n", data->entries[i].hash, temp);
			}
			fprintf(csvf, "};\n");
			WriteClose(csvf);
			Verbose("BO1.h written.\n", namesloaded);
		}
		else
		{
			// or use internal array
			namesloaded = 0;
			for (linenum = 0; linenum < NUM_CSV_ENTRIES; linenum++)
			{
				hash = wheelofdoom_names[linenum].hash;
				for (i = 0; i < (int)data->numentries; i++)
				{
					if (data->entries[i].hash != hash)
						continue;
					namesloaded++;
					sprintf(data->entries[i].name, "%s%s", bigentryautopaths[BIGENTRY_UNKNOWN], wheelofdoom_names[linenum].name);
					break;
				}
			}
			if (namesloaded)
				Verbose("loaded %i internal filenames.\n", namesloaded);
		}
	}

	// load contents
	if (loadfilecontents)
	{
		for (i = 0; i < (int)data->numentries; i++)
		{
			entry = &data->entries[i];
			if (entry->size <= 0)
				continue;

			Pacifier("loading entry %i of %i...", i + 1, data->numentries);

			entry->data = qmalloc(entry->size);
			BigfileSeekContents(f, entry->data, entry);
		}
		PacifierEnd();
	} 

	return data;
}

// recalculate all file offsets
void BigfileHeaderRecalcOffsets(bigfileheader_t *data, int additionalentries)
{
	bigfileentry_t *entry;
	int i, offset;

	offset = sizeof(unsigned int) + (data->numentries + additionalentries)*12;
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		entry->oldoffset = entry->offset;
		entry->offset = (unsigned int)offset;
		offset = offset + entry->size;
	}
}

// print stats about loaded bigfile entry
void BigfileEmitStats(bigfileheader_t *data)
{
	bigfileentry_t *entry;
	int stats[BIGFILE_NUM_FILETYPES], timstats[4], rawstats[NUM_RAW_TYPES], nullfiles;
	int i;

	// collect stats
	nullfiles = 0;
	memset(stats, 0, sizeof(stats));
	memset(timstats, 0, sizeof(timstats));
	memset(rawstats, 0, sizeof(rawstats));
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		if (entry->size == 0)
			nullfiles++;
		else
		{
			if (entry->type == BIGENTRY_TIM)
			{
				if (entry->timtype[0] == TIM_4Bit)
					timstats[0]++;
				else if (entry->timtype[0] == TIM_8Bit)
					timstats[1]++;
				else if (entry->timtype[0] == TIM_16Bit)
					timstats[2]++;	
				else if (entry->timtype[0] == TIM_24Bit)
					timstats[3]++;
			}
			else if (entry->type == BIGENTRY_SPRITE)
				rawstats[entry->rawinfo->type]++;
			stats[entry->type]++;
		}
	}

	// print stats
	if (stats[BIGENTRY_RAW_ADPCM])
		Print(" %6i raw ADPCM\n", stats[BIGENTRY_RAW_ADPCM]);
	if (stats[BIGENTRY_RIFF_WAVE])
		Print(" %6i RIFF WAVE\n", stats[BIGENTRY_RIFF_WAVE]);
	// TIM
	if (timstats[0])
		Print(" %6i  4-bit TIM\n", timstats[0]);
	if (timstats[1])
		Print(" %6i  8-bit TIM\n", timstats[1]);
	if (timstats[2])
		Print(" %6i  16-bit TIM\n", timstats[2]);
	if (timstats[3])
		Print(" %6i  24-bit TIM\n", timstats[3]);
	if (stats[BIGENTRY_TIM])
		Print(" %6i TIM total\n", stats[BIGENTRY_TIM]);
	// RAW
	if (rawstats[RAW_TYPE_0])
		Print(" %6i  sprite type 0\n", rawstats[RAW_TYPE_0]);
	if (rawstats[RAW_TYPE_1])
		Print(" %6i  sprite type 1\n", rawstats[RAW_TYPE_1]);
	if (rawstats[RAW_TYPE_2])
		Print(" %6i  sprite type 2\n", rawstats[RAW_TYPE_2]);
	if (rawstats[RAW_TYPE_3])
		Print(" %6i  sprite type 3\n", rawstats[RAW_TYPE_3]);
	if (rawstats[RAW_TYPE_4])
		Print(" %6i  sprite type 4\n", rawstats[RAW_TYPE_4]);
	if (rawstats[RAW_TYPE_5])
		Print(" %6i  sprite type 5\n", rawstats[RAW_TYPE_5]);
	if (stats[BIGENTRY_SPRITE])
		Print(" %6i sprites total\n", stats[BIGENTRY_SPRITE]);
	if (stats[BIGENTRY_VAG])
		Print(" %6i VAG\n", stats[BIGENTRY_VAG]);
	if (stats[BIGENTRY_TILEMAP])
		Print(" %6i tilemaps\n", stats[BIGENTRY_TILEMAP]);
	if (stats[BIGENTRY_MAP])
		Print(" %6i maps\n", stats[BIGENTRY_MAP]);

	// total
	if (stats[BIGENTRY_UNKNOWN])
		Print(" %6i unknown\n", stats[BIGENTRY_UNKNOWN]);
	if (nullfiles)
		Print(" %6i null\n", nullfiles);
	Verbose(" %6i TOTAL\n", data->numentries);
}

// read bigfile header from listfile
bigfileheader_t *BigfileOpenListfile(char *srcdir)
{
	bigfileheader_t *data;
	bigfileentry_t *entry;
	char line[256], temp[128], filename[MAX_BLOODPATH];
	int numentries, linenum, val, num;
	unsigned int uval;
	short valshort;
	FILE *f;

	// open file
	sprintf(filename, "%s/listfile.txt", srcdir);
	f = SafeOpen(filename, "r");

	// read number of entries
	data = qmalloc(sizeof(bigfileheader_t));
	if (fscanf(f, "numentries=%i\n", &numentries) != 1)
		Error("broken numentries record");
	Verbose("%s: %i entries\n", filename, numentries);

	// read all entries
	linenum = 1;
	entry = NULL;
	data->entries = qmalloc(numentries * sizeof(bigfileentry_t));
	data->numentries = 0;
	while(!feof(f))
	{
		linenum++;
		fgets(line, 256, f);

		// comments
		if (!line[0] || line[0] == '#')
			continue;

		// new entry
		if (line[0] == '[')
		{
			if (sscanf(line, "[%X]", &val) < 1)
				Error("bad entry definition on line %i: %s\n", linenum, line);
			if ((int)data->numentries >= numentries)
				Error("entries overflow, numentries is out of date\n");

			entry = &data->entries[data->numentries];
			BigfileEmptyEntry(entry);
			entry->hash = (unsigned int)val;
			data->numentries++;
			
			Pacifier("reading entry %i of %i...", data->numentries, numentries);
			continue;
		}

		// scan parameter
		if (entry == NULL)
			Error("Entry data without actual entry on line %i: %s\n", linenum, line);

		// parse base parms
		if (sscanf(line, "type=%i", &val))
		{
			entry->type = (bigentrytype_t)val;
			if (entry->type == BIGENTRY_SPRITE)
				entry->rawinfo = NewRawInfo();
		}
		else if (sscanf(line, "size=%i", &val))
			entry->size = (int)val;
		else if (sscanf(line, "offset=%i", &val))
			entry->offset = (int)val;
		else if (sscanf(line, "file=%s", &temp))
			strcpy(entry->name, temp);
		// for TIM
		else if (sscanf(line, "tim[%i].type=%i", &num, &uval))
			entry->timtype[num] = uval;
		else if (sscanf(line, "tim[%i].xpos=%f", &num, &valshort))
			entry->timxpos[num] = valshort;
		else if (sscanf(line, "tim[%i].ypos=%f", &num, &valshort))
			entry->timypos[num] = valshort;
		else if (sscanf(line, "tim.layers=%i", &val))
			entry->timlayers = val;
		// for VAG
		else if (sscanf(line, "adpcm.rate=%i", &val))
			entry->adpcmrate = val;
		// for raw
		else if (entry->type == BIGENTRY_SPRITE)
			ReadRawInfo(line, entry->rawinfo);
	}
	PacifierEnd();

	// emit some ststs
	BigfileEmitStats(data);

	return data;
}

/*
==========================================================================================

  Extracting entries from bigfile

==========================================================================================
*/

void TGAfromTIM(FILE *bigf, bigfileentry_t *entry, char *outfile, qboolean bpp16to24)
{
	char name[MAX_BLOODPATH], maskname[MAX_BLOODPATH], suffix[21];
	tim_image_t *tim;
	int i;

	BigfileSeekFile(bigf, entry);
	for (i = 0; i < entry->timlayers; i++)
	{
		// extract base
		if (entry->data != NULL)
			tim = TIM_LoadFromBuffer(entry->data, entry->size);
		else
			tim = TIM_LoadFromStream(bigf);
		strcpy(name, outfile);
		if (i != 0)
		{
			sprintf(suffix, "_sub%i", i);
			AddSuffix(name, name, suffix);
		}
		if (tim->error)
			Error("error saving %s: %s\n", name, tim->error);
		// write basefile
		TIM_WriteTarga(tim, name, bpp16to24);
		// write maskfile
		if (tim->pixelmask != NULL)
		{
			sprintf(suffix, "_mask");
			AddSuffix(maskname, name, suffix);
			TIM_WriteTargaGrayscale(tim->pixelmask, tim->dim.xsize, tim->dim.ysize, maskname);
		}
		FreeTIM(tim);
	}
}

void TGAfromRAW(rawblock_t *rawblock, rawinfo_t *rawinfo, char *outfile, qboolean rawnoalign, qboolean verbose, qboolean usesubpaths)
{
	char name[MAX_BLOODPATH], suffix[8], path[MAX_BLOODPATH], basename[MAX_BLOODPATH], filename[MAX_BLOODPATH];
	int maxwidth, maxheight, i;

	// detect maxwidth/maxheight for alignment
	maxwidth = maxheight = 0;
	for (i = 0; i < rawblock->chunks; i++)
	{
		maxwidth = max(maxwidth, (rawblock->chunk[i].width + rawblock->chunk[i].x));
		maxheight = max(maxheight, (rawblock->chunk[i].height + rawblock->chunk[i].y));
	}

	// quick fix for files in separate folders
	// todo: optimize
	if (usesubpaths)
	{
		ExtractFilePath(outfile, path);
		ExtractFileName(outfile, filename);
		ConvDot(filename);
		ExtractFileBase(outfile, basename);
		sprintf(name, "%s%s/", path, filename);
		CreatePath(name);
		sprintf(name, "%s%s/%s.tga", path, filename, basename);
		strcpy(basename, name);
	}
	else
		sprintf(basename, "%s.tga", outfile);

	// export all chunks
	for (i = 0; i < rawblock->chunks; i++)
	{
		if (rawinfo->chunknum != -1 && i != rawinfo->chunknum)
			continue; // skip this chunk
		strcpy(name, basename);
		if (rawblock->chunks != 1)
		{
			sprintf(suffix, "_%03i", i);
			AddSuffix(name, name, suffix);
		}
		if (verbose == true)
			Print("writing %s.\n", name);
		if (rawnoalign)
			RawTGA(name, rawblock->chunk[i].width, rawblock->chunk[i].height, 0, 0, 0, 0, rawblock->chunk[i].colormap ? rawblock->chunk[i].colormap : rawblock->colormap, rawblock->chunk[i].pixels, 8, rawinfo);
		else
			RawTGA(name, rawblock->chunk[i].width, rawblock->chunk[i].height, rawblock->chunk[i].x, rawblock->chunk[i].y, max(0, maxwidth - rawblock->chunk[i].width - rawblock->chunk[i].x), max(0, maxheight - rawblock->chunk[i].height - rawblock->chunk[i].y), rawblock->chunk[i].colormap ? rawblock->chunk[i].colormap : rawblock->colormap, rawblock->chunk[i].pixels, 8, rawinfo);
	}
}

// unpack entry to 'original' dir, assumes that entity is already loaded
void BigFileUnpackOriginalEntry(bigfileentry_t *entry, char *dstdir, qboolean place_separate, qboolean correct_listfile)
{
	char filename[MAX_BLOODPATH], savefile[MAX_BLOODPATH];

	if (place_separate)
	{
		if (correct_listfile)
		{
			ExtractFileName(entry->name, savefile);
			sprintf(entry->name, "original/%s", savefile);
			sprintf(savefile, "%s/%s", dstdir, entry->name);
		}
		else
		{
			ExtractFileName(entry->name, filename);
			sprintf(savefile, "%s/original/%s", dstdir, filename);
		}
	}
	else
		sprintf(savefile, "%s/%s", dstdir, entry->name);

	SaveFile(savefile, entry->data, entry->size);
}

int MapExtract(char *mapfile, byte *fileData, int fileDataSize, char *outfile, bigfileheader_t *bigfileheader, FILE *bigfile, char *tilespath, qboolean with_solid, qboolean with_triggers, qboolean toggled_objects, qboolean developer, int devnum);
void BigFileUnpackEntry(bigfileheader_t *bigfileheader, FILE *bigf, bigfileentry_t *entry, char *dstdir, qboolean tim2tga, qboolean bpp16to24, qboolean nopaths, int adpcmconvert, int vagconvert, qboolean rawconvert, rawtype_t forcerawtype, qboolean rawnoalign, qboolean map2tga, qboolean map_show_contents, qboolean map_show_triggers, qboolean map_toggled_objects)
{
	char savefile[MAX_BLOODPATH], outfile[MAX_BLOODPATH], basename[MAX_BLOODPATH], path[MAX_BLOODPATH];
	char inputcmd[512], outputcmd[512];
	rawblock_t *rawblock;
	byte *data, *outdata;
	qboolean oldprint;
	int c, size, outsize;

	// nopaths, clear path
	if (nopaths)
	{
		ExtractFileBase(entry->name, path);
		strcpy(entry->name, path);
	}

	// original pill.big has 'funky' files with zero len, export them as empty ones
	if (entry->size <= 0)
		return;

	// make directory
	ExtractFilePath(entry->name, path);
	if (path[0])
	{
		sprintf(savefile, "%s/%s", dstdir, path);
		CreatePath(savefile);
	}

	// load file contents
	if (entry->data == NULL)
	{
		entry->data = qmalloc(entry->size);
		BigfileSeekContents(bigf, entry->data, entry);
	}

	// autoconvert TGA
	if (tim2tga && entry->type == BIGENTRY_TIM)
	{
		// unpack original anyway as we may need it (for map extraction for example)
		//BigFileUnpackOriginalEntry(entry, dstdir, true, false);
		qfree(entry->data);
		entry->data = NULL;
		// extract TIM
		ExtractFileBase(entry->name, basename);
		ExtractFilePath(entry->name, path);
		sprintf(entry->name, "%s%s.tga", path, basename); // write correct listfile.txt
		sprintf(outfile, "%s/%s%s.tga", dstdir, path, basename);
		TGAfromTIM(bigf, entry, outfile, bpp16to24);
		return;
	}

	// autoconvert TGA (for tiles)
	// fixme: both-way conversion?
	if (tim2tga && entry->type == BIGENTRY_TILEMAP)
	{
		ExtractFileBase(entry->name, basename);
		ExtractFilePath(entry->name, path);
		sprintf(outfile, "%s/%s%s.tga", dstdir, path, basename);
		// write original
		BigFileUnpackOriginalEntry(entry, dstdir, true, true);
		// decode
		outsize = entry->size;
		data = LzDec(&size, entry->data, 0, outsize, true);
		qfree(entry->data);
		entry->timlayers = 1;
		entry->data = data;
		entry->size = size;
		// write
		TGAfromTIM(bigf, entry, outfile, bpp16to24);
		// cleanup, restore size
		qfree(entry->data);
		entry->timlayers = 0;
		entry->size = outsize;
		entry->data = NULL;
		return;
	}

	// autoconvert map
	if (map2tga && entry->type == BIGENTRY_MAP)
	{
		// write
		ExtractFileBase(entry->name, basename);
		ExtractFilePath(entry->name, path);
		oldprint = noprint;
		noprint = true;
		sprintf(outfile, "%s/%s%s.tga", dstdir, path, basename);
		MapExtract(basename, entry->data, entry->size, outfile, bigfileheader, bigf, "", map_show_contents, map_show_triggers, map_toggled_objects, false, 0);
		noprint = oldprint;
		// write original
		BigFileUnpackOriginalEntry(entry, dstdir, true, true);
		// clean
		qfree(entry->data);
		entry->data = NULL;
		return;
	}
	
	// autoconvert raw ADPCM or VAG
	if ((adpcmconvert && entry->type == BIGENTRY_RAW_ADPCM) || (vagconvert && entry->type == BIGENTRY_VAG))
	{
		StripFileExtension(entry->name, basename);
		if (entry->type == BIGENTRY_RAW_ADPCM)
		{
			c = adpcmconvert;
			data = entry->data;
			size = entry->size;
			sprintf(inputcmd, "-t ima -r %i -c 1", entry->adpcmrate);
		}
		else
		{
			c = vagconvert;
			// unpack vag
			VAG_Unpack(entry->data, 64, entry->size, &data, &size);
			sprintf(inputcmd, "-t s16 -r %i -c 1", entry->adpcmrate);
		}

		// try to save
		sprintf(savefile, (c == 3) ? "%s/%s.ogg" : "%s/%s.wav", dstdir, basename);
		if (c == 3)
			sprintf(outputcmd, "-t ogg -C 7");
		else if (c == 2)
			sprintf(outputcmd, "-t wav -e signed-integer");
		else 
			sprintf(outputcmd, "-t wav");
		if (SoX_DataToData(data, size, "--no-dither", inputcmd, outputcmd, &outdata, &outsize, ""))
		{
			sprintf(entry->name, (c == 3) ? "%s.ogg" : "%s.wav", basename);  // write correct listfile.txt
			SaveFile(savefile, outdata, outsize);
			qfree(outdata);
		}
		else
		{
			//Warning("unable to convert %s, SoX Error #%i, unpacking original", entry->name, GetLastError());
			Error("unable to convert %s, SoX Error, unpacking original", entry->name);
			BigFileUnpackOriginalEntry(entry, dstdir, false, false);
		}

		// for VAG, unpack original anyway
		// fixme: make backwards conversion
		if (entry->type == BIGENTRY_VAG)
			BigFileUnpackOriginalEntry(entry, dstdir, true, true);

		if (data != entry->data)
			qfree(data);
		qfree(entry->data);
		entry->data = NULL;
		return;
	}

	// convert raw file
	if (rawconvert && entry->type == BIGENTRY_SPRITE)
	{
		rawblock = RawExtract(entry->data, entry->size, entry->rawinfo, true, false, forcerawtype);
		if (rawblock->errorcode < 0)
			Print("warning: cound not extract raw %s: %s\n", entry->name, RawStringForResult(rawblock->errorcode));
		else
		{
			sprintf(outfile, "%s/%s", dstdir, entry->name);
			TGAfromRAW(rawblock, entry->rawinfo, outfile, rawnoalign, false, (rawblock->chunks > 1) ? true : false); 
			// unpack tail files
			if (rawblock->errorcode > 0 && rawblock->errorcode < (int)entry->size)
				RawExtractTGATailFiles((byte *)entry->data + rawblock->errorcode, entry->size - rawblock->errorcode, entry->rawinfo, outfile, false, (rawblock->chunks > 1) ? true : false, rawnoalign);
		}
		FreeRawBlock(rawblock);
		// unpack original
		BigFileUnpackOriginalEntry(entry, dstdir, true, true);
		qfree(entry->data);
		entry->data = NULL;
		return;
	}

	// convert wave file
	if (entry->type == BIGENTRY_RIFF_WAVE)
	{
		// change file extension to wav and write original
		StripFileExtension(entry->name, basename);
		sprintf(entry->name, "%s.wav", basename);
	}

	// unpack original
	BigFileUnpackOriginalEntry(entry, dstdir, false, false);
	qfree(entry->data);
	entry->data = NULL;
}

/*
==========================================================================================

  BigFile filetype scanner

==========================================================================================
*/

qboolean BigFileScanTIM(FILE *f, bigfileentry_t *entry)
{
	tim_image_t *tim;
	fpos_t fpos;
	unsigned int tag;
	unsigned int bpp;
	int bytes;

	// VorteX: Blood Omen has *weird* TIM files - they could be 2 or more TIM's in one file
	// they could be easily detected however, as second TIM goes right after base TIM 

	BigfileSeekFile(f, entry);

	bytes = 0;
	entry->timlayers = 0;
	while(1)
	{
		fgetpos(f, &fpos);

		// 0x10 should be at beginning of standart TIM
		if (fread(&tag, sizeof(unsigned int), 1, f) < 1)
			return (entry->timlayers != 0) ? true : false;
		if (tag != 0x10)
			return (entry->timlayers != 0) ? true : false;

		// second uint is BPP
		// todo: there are files with TIM header but with nasty BPP
		if (fread(&bpp, sizeof(unsigned int), 1, f) < 1)
			return (entry->timlayers != 0) ? true : false;
		if (bpp != TIM_4Bit && bpp != TIM_8Bit && bpp != TIM_16Bit && bpp != TIM_24Bit) 
			return (entry->timlayers != 0) ? true : false;

		// try load that TIM
		fsetpos(f, &fpos);
		tim = TIM_LoadFromStream(f);
		if (tim->error)
		{
			FreeTIM(tim);
			return false;
		}
		bytes += tim->filelen;

		// fill diminfo section
		if (entry->timlayers >= (MAX_TIM_MASKS + 1))
			Error("TIM layers overflow on entry %.8X", entry->hash);

		entry->timtype[entry->timlayers] = tim->type;
		entry->timxpos[entry->timlayers] = tim->dim.xpos;
		entry->timypos[entry->timlayers] = tim->dim.ypos;
		entry->timlayers++;
		FreeTIM(tim);

		if (bytes >= (int)entry->size)
			break;
	}

	return true;
}

qboolean BigFileScanRiffWave(FILE *f, bigfileentry_t *entry)
{
	byte tag[4];

	BigfileSeekFile(f, entry);

	// first unsigned int - tag
	if (fread(&tag, sizeof(char), 4, f) < 1)
		return false;
	if (tag[0] != 0x52 || tag[1] != 0x49 || tag[2] != 0x46 || tag[3] != 0x46)
		return false;

	// it's a RIFF
	return true;
}

qboolean BigFileScanVAG(FILE *f, bigfileentry_t *entry)
{
	byte tag[4];

	BigfileSeekFile(f, entry);

	// first unsigned int - tag
	if (fread(&tag, sizeof(char), 4, f) < 1)
		return false;
	if (tag[0] != 'V' || tag[1] != 'A' || tag[2] != 'G' || tag[3] != 'p')
		return false;
	return true;
}

// extensive scan for headerless VAG (by parsing file)
qboolean BigFileScanVAG_PS1(FILE *f, bigfileentry_t *entry)
{
	unsigned int readpos;
	byte *data;

	data = qmalloc(entry->size);
	BigfileSeekContents(f, data, entry);
	readpos = VAG_UnpackTest(data, entry->size, 64);
	qfree(data);
	//if (readpos == entry->size)
	//	printf("%.8X______________\n", entry->hash);
	return (readpos == entry->size) ? true : false;
}

qboolean BigFileScanRaw(FILE *f, bigfileentry_t *entry, rawtype_t forcerawtype)
{
	unsigned char *filedata;
	rawinfo_t *rawinfo;
	rawblock_t *rawblock;

	// load file contents
	BigfileSeekFile(f, entry);
	filedata = qmalloc(entry->size);
	if (fread(filedata, entry->size, 1, f) < 1)
	{
		qfree(filedata);
		return false;
	}

	// check all raw types
	rawinfo = NewRawInfo();
	rawblock = RawExtract(filedata, entry->size, rawinfo, true, false, forcerawtype);
	if (rawblock->errorcode >= 0)
	{
		FreeRawBlock(rawblock);
		entry->rawinfo = rawinfo;
		qfree(filedata);
		return true;
	}
	// not found
	FreeRawBlock(rawblock);
	qfree(filedata);
	qfree(rawinfo);
	return false;
}

bigentrytype_t BigFileScanMapOrTile(FILE *f, bigfileentry_t *entry)
{
	unsigned char *filedata;
	int m;

	// load file
	BigfileSeekFile(f, entry);
	filedata = qmalloc(entry->size);
	if (fread(filedata, entry->size, 1, f) < 1)
	{
		qfree(filedata);
		return BIGENTRY_UNKNOWN;
	}
	// scan and return
	m = MapScan(filedata, entry->size);
	qfree(filedata);
	return m == 1 ? BIGENTRY_MAP : (m == 2) ? BIGENTRY_TILEMAP : BIGENTRY_UNKNOWN;
}

bigentrytype_t BigfileDetectFiletype(FILE *f, bigfileentry_t *entry, qboolean scanraw, rawtype_t forcerawtype)
{
	if (BigFileScanTIM(f, entry))
		return BIGENTRY_TIM;
	if (BigFileScanRiffWave(f, entry))
		return BIGENTRY_RIFF_WAVE;
	if (BigFileScanVAG(f, entry))
		return BIGENTRY_VAG;
	if (scanraw)
		if (BigFileScanRaw(f, entry, forcerawtype))
			return BIGENTRY_SPRITE;
	if (BigFileScanVAG_PS1(f, entry))
		return BIGENTRY_VAG;
	return BigFileScanMapOrTile(f, entry);
}

void BigfileScanFiletype(FILE *f, bigfileentry_t *entry, qboolean scanraw, rawtype_t forcerawtype, qboolean allow_auto_naming)
{
	char name[MAX_BLOODPATH], ext[MAX_BLOODPATH];
	bigentrytype_t autotype;
	bigkentry_t *kentry;
	char *autopath;

	// detect filetype automatically
	autotype = BigfileDetectFiletype(f, entry, scanraw, forcerawtype);
	if (autotype != BIGENTRY_UNKNOWN) 
	{
		entry->type = autotype;
		// automatic path
		if (allow_auto_naming)
		{
			ExtractFileBase(entry->name, name);
			ExtractFileExtension(entry->name, ext);
			autopath = NULL;
			if (autotype == BIGENTRY_SPRITE)
				autopath = PathForRawType(entry->rawinfo->type);
			if (autopath == NULL)
				autopath = bigentryautopaths[autotype];
			if (!strcmp(ext, bigentryext[BIGENTRY_UNKNOWN]))
				sprintf(entry->name, "%s%s.%s", autopath, name, bigentryext[entry->type]);
			else
				sprintf(entry->name, "%s%s.%s", autopath, name, ext);
		}
		// check klist and pick rawinfo anyway
		kentry = BigfileSearchKList(entry->hash);
		if (kentry != NULL)
		{
			entry->adpcmrate = (int)kentry->adpcmrate;
			if (entry->type == BIGENTRY_SPRITE)
				entry->rawinfo = kentry->rawinfo;
		}
	}
	// check listfile
	else
	{
		kentry = BigfileSearchKList(entry->hash);
		if (kentry != NULL)
		{
			entry->type = (bigentrytype_t)kentry->type;
			entry->adpcmrate = (int)kentry->adpcmrate;
			if (entry->type == BIGENTRY_SPRITE)
				entry->rawinfo = kentry->rawinfo;
			// check custom path
			if (allow_auto_naming)
			{
				ExtractFileName(entry->name, name);
				if (kentry->path[0])
				{
					if (kentry->pathonly)
						sprintf(entry->name, "%s/%s", kentry->path, name);
					else
						sprintf(entry->name, "%s", kentry->path);
				}
				else
				{
					ExtractFileName(entry->name, name);
					ExtractFileExtension(entry->name, ext);
					// automatic path
					autopath = NULL;
					if (entry->type == BIGENTRY_SPRITE)
						autopath = PathForRawType(entry->rawinfo->type);
					if (autopath == NULL)
						autopath = bigentryautopaths[autotype];
					if (!strcmp(ext, bigentryext[BIGENTRY_UNKNOWN]))
						sprintf(entry->name, "%s%s.%s", autopath, name, bigentryext[entry->type]);
					else
						sprintf(entry->name, "%s%s.%s", autopath, name, ext);
				}
			}
		}
	}
}

void BigfileScanFiletypes(FILE *f, bigfileheader_t *data, qboolean scanraw, list_t *ixlist, rawtype_t forcerawtype)
{
	fpos_t fpos;
	bigfileentry_t *entry;
	int i;
	
	fgetpos(f, &fpos);
	// scan for filetypes
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];

		// ignore if mismatched
		if (ixlist)
			if (!MatchIXList(entry, ixlist, false, false))
				continue;
		// ignore null-sized
		if (!entry->size)
			continue;
		Pacifier("scanning type for entry %i of %i...", i + 1, data->numentries);
		BigfileScanFiletype(f, entry, scanraw, forcerawtype, /*data->namesfromcsv ? false : */true);
	}
	fsetpos(f, &fpos);
	
	PacifierEnd();

	// emit some stats
	BigfileEmitStats(data);
}


/*
==========================================================================================

  BigFile analyser

==========================================================================================
*/

typedef struct
{
	unsigned int data;
	int occurrences;
}
bigchunk4_t;

typedef struct
{
	unsigned int data;
	int occurrences;
}
bigchunk8_t;

typedef struct
{
	// unsigned int chunks
	bigchunk4_t chunks4[2048];
	byte chunk4;
	int numchunks4;
}
bigchunkstats_t;

/*
==========================================================================================

  -bigfile -list [-to]

  lists bigfile contents

==========================================================================================
*/

int BigFile_List(int argc, char **argv)
{
	FILE *f, *f2;
	bigfileheader_t *data;
	bigfileentry_t *entry;
	char name[MAX_BLOODPATH], listfile[MAX_BLOODPATH], exportcsv[MAX_BLOODPATH], typestr[128], extrainfo[128];
	qboolean hashnamesonly;
	int i;

	// check parms
	strcpy(exportcsv, "-");
	strcpy(listfile, "-");
	hashnamesonly = false;
	if (argc > 0) 
	{
		strcpy(listfile, argv[0]);
		for (i = 0; i < argc; i++)
		{
			if (!strcmp(argv[i], "-exportcsv"))
			{
				i++; 
				if (i < argc)
				{
					strlcpy(exportcsv, argv[i], sizeof(exportcsv));
					Verbose("Option: export CSV file '%s'\n", argv[i]);
				}
				continue;
			}
			if (!strcmp(argv[i], "-hashasnames"))
			{
				hashnamesonly = true;
				Verbose("Option: use pure hash names\n");
				continue;
			}
			if (i != 0)
				Warning("unknown parameter '%s'",  argv[i]);
		}
	}

	// open file & load header
	f = SafeOpen(bigfile, "rb");
	data = ReadBigfileHeader(f, false, hashnamesonly);
	BigfileScanFiletypes(f, data, true, NULL, RAW_TYPE_UNKNOWN);

	// print or...
	if (listfile[0] == '-')
		BigfileWriteListfile(stdout, data);
	else // output to file
	{
		f2 = SafeOpenWrite(listfile);
		BigfileWriteListfile(f2, data);
		Print("wrote %s\n", listfile);
		WriteClose(f2);
	}

	// export CSV file
	if (exportcsv[0] != '-')
	{
		Print("Exporting %s...\n", exportcsv);
		f2 = SafeOpenWrite(exportcsv);
		for (i = 0; i < (int)data->numentries; i++)
		{
			entry = &data->entries[i];
			// base info
			if (entry->type == BIGENTRY_SPRITE)
				sprintf(typestr, "%s.%s", bigentryext[entry->type], UnparseRawType(entry->rawinfo->type));
			else
				sprintf(typestr, "%s", bigentryext[entry->type]);
			// extra info
			if (entry->type == BIGENTRY_RAW_ADPCM)
				sprintf(extrainfo, "%i", entry->adpcmrate);
			else if (entry->type == BIGENTRY_SPRITE)
			{
				if (entry->rawinfo->type == RAW_TYPE_2 && entry->rawinfo->doubleres == true)
					sprintf(extrainfo, "doubleres");
				else
					sprintf(extrainfo, "");
			}	
			else sprintf(extrainfo, "");
			// print
			strcpy(name, entry->name);
			ConvSlashU2W(name);
			fprintf(f2, "%i;%s;%s;%s;\n", entry->hash, name, typestr, extrainfo);
		}
		WriteClose(f2);
	}

/*
	f2 = SafeOpenWrite("list.log");
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		if (entry->type == BIGENTRY_SPRITE)
		{
			if (entry->rawinfo->type ==
				sprintf(typestr, "%s.%s", bigentryext[entry->type], UnparseRawType(entry->rawinfo->type));

		else if (entry->type == BIGENTRY_SPRITE)
			sprintf(eypestr, "%s.%s", bigentryext[entry->type], UnparseRawType(entry->rawinfo->type));

	}
	fclose(f);
*/

	Print("done.\n");
	fclose (f);
	return 0;
}

/*
==========================================================================================

  -bigfile -extract

  extract single entity

==========================================================================================
*/

void BigFile_ExtractRawImage(int argc, char **argv, char *outfile, bigfileentry_t *entry, rawblock_t *rawblock, char *format)
{
	int i, num, minp, maxp, margin, aver, diff, spritex, spritey, spriteflags;
	sprtype_t spritetype = SPR_VP_PARALLEL;
	rawblock_t *tb1, *tb2, *tb3, *tb4;
	qboolean noalign, nocrop, flip, scale, merge;
	byte pix, shadowpix;
	byte c[3], oldcolormap[768], oldalphamap[256], loadedcolormap[768];
	double colorscale, cscale, alphascale;
	list_t *includelist;
	FILE *f;

	// additional parms
	includelist = NewList();
	noalign = false;
	nocrop = false;
	flip = false;
	margin = 1;
	spritex = 0;
	spritey = 0;
	spriteflags = 0;
	shadowpix = 15;
	alphascale = 1.0f;
	scale = false;
	merge = false;
	// copy out colormap & alphamap
	if (rawblock->colormap)
		memcpy(oldcolormap, rawblock->colormap, 768);
	if (rawblock->alphamap)
		memcpy(oldalphamap, rawblock->alphamap, 256);
	// process command line
	for (i = 0; i < argc; i++)
	{
		if (!strcmp(argv[i], "-oriented"))
		{
			spritetype = SPR_ORIENTED;
			Verbose("Option: sprite type = ORIENTED\n");
			continue;
		}
		if (!strcmp(argv[i], "-parallel_upright"))
		{
			spritetype = SPR_VP_PARALLEL_UPRIGHT;
			Verbose("Option: sprite type = PARALLEL_UPRIGHT\n");
			continue;
		}
		if (!strcmp(argv[i], "-facing_upright"))
		{
			spritetype = SPR_VP_FACING_UPRIGHT;
			Verbose("Option: sprite type = FACING_UPRIGHT\n");
			continue;
		}
		if (!strcmp(argv[i], "-parallel"))
		{
			spritetype = SPR_VP_PARALLEL;
			Verbose("Option: sprite type = PARALLEL\n");
			continue;
		}
		if (!strcmp(argv[i], "-overhead"))
		{
			spritetype = SPR_OVERHEAD;
			Verbose("Option: sprite type = OVERHEAD\n");
			continue;
		}
		if (!strcmp(argv[i], "-i"))
		{
			i++; 
			if (i < argc)
			{
				ListAdd(includelist, argv[i], true);
				Verbose("Option: include chunks '%s'\n", argv[i]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-bgcolor"))
		{
			i++;
			if (i < argc)
			{
				num = ParseHex(argv[i]);
				rawblock->colormap[0] = (byte)((num >> 16) & 0xFF);
				rawblock->colormap[1] = (byte)((num >> 8) & 0xFF);
				rawblock->colormap[2] = (byte)(num & 0xFF);
				Verbose("Option: custom background color '%i %i %i'\n", rawblock->colormap[0], rawblock->colormap[1], rawblock->colormap[2]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-altcolor"))
		{
			i++;
			if (i < argc)
			{
				pix = (byte)atoi(argv[i]);
				i++;
				if (i < argc)
				{
					num = ParseHex(argv[i]);
					rawblock->colormap[pix*3] = (byte)((num >> 16) & 0xFF);
					rawblock->colormap[pix*3 + 1] = (byte)((num >> 8) & 0xFF);
					rawblock->colormap[pix*3 + 2] = (byte)(num & 0xFF);
					Verbose("Option: replace colormap index #%i by '%i %i %i'\n", rawblock->colormap[pix*3 + 0], rawblock->colormap[pix*3 + 1], rawblock->colormap[pix*3 + 2]);
				}
			}
			continue;
		}
		if (!strcmp(argv[i], "-colormapscale"))
		{
			i++;
			if (i < argc)
			{
				colorscale = atof(argv[i]);
				for (num = 0; num < 768; num++)
					rawblock->colormap[num] = (byte)max(0, min(rawblock->colormap[num]*colorscale, 255));
				Verbose("Option: scale colormap colors by %f'\n", colorscale);
			}
			continue;
		}
		if (!strcmp(argv[i], "-colormapsub"))
		{
			i++;
			if (i < argc)
			{
				colorscale = atof(argv[i]);
				for (num = 0; num < 768; num++)
					rawblock->colormap[num] = (byte)max(0, min(rawblock->colormap[num] - colorscale, 255));
				Verbose("Option: subtract colormap colors by %f'\n", colorscale);
			}
			continue;
		}
		if (!strcmp(argv[i], "-shadowpixel"))
		{
			i++;
			if (i < argc)
			{
				shadowpix = (byte)atoi(argv[i]);
				Verbose("Option: shadow pixel index #%i\n", shadowpix);
			}
			continue;
		}
		if (!strcmp(argv[i], "-shadowcolor"))
		{
			i++;
			if (i < argc)
			{
				num = ParseHex(argv[i]);
				if (!rawblock->colormap)
					Warning("cannot set shadow color, rawfile has no shared palette");
				else
				{
					rawblock->colormap[shadowpix*3] = (byte)((num >> 16) & 0xFF);
					rawblock->colormap[shadowpix*3 + 1] = (byte)((num >> 8) & 0xFF);
					rawblock->colormap[shadowpix*3 + 2] = (byte)(num & 0xFF);
					Verbose("Option: custom shadow color '%i %i %i'\n", rawblock->colormap[shadowpix*3], rawblock->colormap[shadowpix*3 + 1], rawblock->colormap[shadowpix*3 + 2]);
				}
			}
			continue;
		}
		if (!strcmp(argv[i], "-shadowalpha"))
		{
			i++;
			if (i < argc)
			{
				if (!rawblock->alphamap)
					Warning("cannot set shadow alpha, rawfile has no shared alphamap");
				else
				{
					rawblock->alphamap[shadowpix] = (byte)atoi(argv[i]);
					Verbose("Option: custom shadow alpha %i\n", rawblock->alphamap[shadowpix]);
				}
			}
			continue;
		}
		if (!strcmp(argv[i], "-additive"))
		{
			if (!rawblock->colormap)
				Warning("cannot apply alphamap, rawfile has no shared palette");
			else if (!rawblock->alphamap)
				Warning("cannot apply alphamap, rawfile has no shared alphamap");
			else
			{
				Verbose("Option: nullifying alpha for black pixels\n");
				for (num = 0; num < 256; num++)
					if (!rawblock->colormap[num*3] && !rawblock->colormap[num*3 + 1] && !rawblock->colormap[num*3 + 2])
						rawblock->alphamap[num] = 0;
			}
			continue;
		}
		if (!strcmp(argv[i], "-alpha"))
		{
		
			i++;
			if (i < argc)
			{
				alphascale = atof(argv[i]);
				Verbose("Option: alpha scaled by %f\n", alphascale);
			}
			continue;
		}
		if (!strcmp(argv[i], "-replacecolormap"))
		{
			i++;
			if (i < argc)
			{
				Verbose("Option: replacing colormap from %s\n", argv[i]);
				if (!rawblock->colormap)
					Warning("cannot replace colormap, rawfile has no shared palette");
				else if (FileExists(argv[i]))
					ColormapFromTGA(argv[i], rawblock->colormap);
				else
					Warning("cannot replace colormap, %s not found", argv[i]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-applyalphamap"))
		{
			i++;
			if (i < argc)
			{
				Verbose("Option: applying alphamap from %s\n", argv[i]);
				if (!rawblock->colormap)
					Warning("cannot apply alphamap, rawfile has no shared palette");
				else if (!rawblock->alphamap)
					Warning("cannot apply alphamap, rawfile has no shared alphamap");
				else if (FileExists(argv[i]))
				{
					ColormapFromTGA(argv[i], loadedcolormap);
					for (num = 0; num < 256; num++)
					{
						rawblock->alphamap[num] = loadedcolormap[num*3];
						if (!rawblock->alphamap[num])
							memset(rawblock->colormap + num*3, 0, 3); // black, alpha 0
					}
				}
				else
					Warning("cannot apply alphamap, %s not found", argv[i]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-noalign"))
		{
			noalign = true;
			Verbose("Option: Disable chunks aligning\n");
			continue;
		}
		if (!strcmp(argv[i], "-nocrop"))
		{
			nocrop = true;
			Verbose("Option: Disable null pixels cropping\n");
			continue;
		}
		if (!strcmp(argv[i], "-margin"))
		{
			i++;
			if (i < argc)
			{
				margin = atoi(argv[i]);
				if (margin < 0)
					margin = 0;
				if (margin > 100)
					margin = 100;
				Verbose("Option: %i-pixel margin\n", margin);
			}
			continue;
		} 
		if (!strcmp(argv[i], "-flip"))
		{
			flip = true;
			Verbose("Option: horizontal flipping\n", margin);
			continue;
		}
		if (!strcmp(argv[i], "-ofs"))
		{
			i++;
			if ((i + 1) < argc)
			{
				spritex = atoi(argv[i]);
				i++;
				spritey = atoi(argv[i]);
				Verbose("Option: offset sprite by x = %i, y = %i\n", spritex, spritey);
			}
			continue;
		}
		if (!strcmp(argv[i], "-nearest2x"))
		{
			scale = true;
			Verbose("Option: Scale by factor 2 with nearest neighbour\n");
			continue;
		}
		if (!strcmp(argv[i], "-colormap2nsx"))
		{
			i++;
			if ((i + 4) < argc)
			{
				f = fopen(argv[i+4], "a");
				if (!f)
					Warning("Option: cannot open %s for input\n", argv[i+3]);
				else
				{
					fputs(argv[i+3], f);
					fputs("=", f);
					// write colormap
					minp = min(255, max(0, atoi(argv[i])));
					maxp = min(255, max(0, atoi(argv[i+1])));
					cscale = atof(argv[i+2]);
					for (num = minp; num < maxp; num++)
					{
						memcpy(c, rawblock->colormap + num*3, 3);
						aver = (int)((c[0] + c[1] + c[2])/3);
						diff = max(c[0], max(c[1], c[2]));
						// reject any color thats too gray
						if (!diff || aver/diff > 0.8)
							continue;
						fprintf(f, "'%i %i %i'", (int)(c[0]*cscale), (int)(c[1]*cscale), (int)(c[2]*cscale));
					}
					fputs("\n", f);
					fclose(f);
					Verbose("Option: Export palette indexes %s-%s scale %s as #%s to %s\n", argv[i], argv[i+1], argv[i+2], argv[i+3], argv[i+4]);
				}
				i += 4;
				continue;
			}
			continue;
		}
		if (!strcmp(argv[i], "-m"))
		{
			merge = true;
			Verbose("Option: Merge output instead of replacing if file already exists\n");
			continue;
		}
		Warning("unknown parameter '%s'",  argv[i]);
	}

	// perturbare rawblock
	tb1 = NULL;
	tb2 = NULL;
	tb3 = NULL;
	tb4 = NULL;
	if (includelist->items)
	{
		Print("Perturbating...\n");
		rawblock = tb1 = RawblockPerturbate(rawblock, includelist);
	}
	// aligning/cropping/flipping (alternate offsetting) or just flipping (original offsetting)
	if (!noalign)
	{
		Print("Aligning...\n");
		rawblock = tb2 = RawblockAlign(rawblock, margin);
		// go crop unused
		if (!nocrop)
		{
			Print("Cropping...\n");
			rawblock = tb3 = RawblockCrop(rawblock, false, margin);
		}
		// go flip
		if (flip)
		{
			Print("Flipping...\n");
			RawblockFlip(rawblock, false);
		}
	}
	else if (flip)
	{
		Print("Flipping...\n");
		RawblockFlip(rawblock, true);
	}
	// scaling
	if (scale)
	{
		Print("Scaling...\n");
		spritex = spritex * 2;
		spritey = spritey * 2;
		rawblock = tb4 = RawblockScale2x_Nearest(rawblock);
	}

	// write file
	Print("Writing images...\n");
	if (!stricmp(format, "spr"))
		Error("Quake sprites format is not supported!\n");
	else if (!stricmp(format, "spr32"))
	{
		if (noalign)
		{
			for (i = 0; i < rawblock->chunks; i++)
			{
				rawblock->chunk[i].x = (0 - rawblock->chunk[i].width) - rawblock->chunk[i].x;
				rawblock->chunk[i].y = 0 - rawblock->chunk[i].y;
			}
		}
		SPR_WriteFromRawblock(rawblock, outfile, SPR_DARKPLACES, spritetype, spritex, spritey, (float)alphascale, spriteflags, merge);
		// extract tail files
		if (rawblock->errorcode > 0 && rawblock->errorcode < (int)entry->size)
			Print("Tail files found but can't be extracted to spr32\n");
	}
	else if (!stricmp(format, "tga"))
	{
		TGAfromRAW(rawblock, entry->rawinfo, outfile, noalign, true, false);
		// extract tail files
		if (rawblock->errorcode > 0 && (int)rawblock->errorcode < (int)entry->size)
			RawExtractTGATailFiles((byte *)entry->data + rawblock->errorcode, entry->size - rawblock->errorcode, entry->rawinfo, outfile, true, false, noalign);	
	}
	else
		Error("unknown sprite format '%s'!\n", format);
	Print("done.\n");

	// restore colormap & alphamap
	if (rawblock->colormap)
		memcpy(rawblock->colormap, oldcolormap, 768);
	if (rawblock->alphamap)
		memcpy(rawblock->alphamap, oldalphamap, 256);

	// free allocated data
	if (tb1) FreeRawBlock(tb1);
	if (tb2) FreeRawBlock(tb2);
	if (tb3) FreeRawBlock(tb3);
	if (tb4) FreeRawBlock(tb4);
}

void BigFile_ExtractSound(int argc, char **argv, char *outfile, bigfileentry_t *entry, char *infileformat, int defaultinputrate, char *format)
{
	char informat[1024], outformat[1024], effects[1024], temp[1024];
	byte *outdata;
	unsigned int outsize;
	int i, ir;

	if (!soxfound)
		Error("SoX not found!");

	// additional parms
	ir = defaultinputrate;
	strcpy(effects, "");
	for (i = 0; i < argc; i++)
	{
		if (!strcmp(argv[i], "-trimstart"))
		{
			i++;
			if (i < argc)
			{
				strcpy(temp, effects);
				sprintf(effects, "%s trim %s", temp, argv[i]);
				Verbose("Option: trim start by %s seconds\n", argv[i]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-speed"))
		{
			i++;
			if (i < argc)
			{
				strcpy(temp, effects);
				sprintf(effects, "%s speed %s", temp, argv[i]);
				Verbose("Option: sound speed %sx\n", argv[i]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-tempo"))
		{
			i++;
			if ((i+1) < argc)
			{
				strcpy(temp, effects);
				sprintf(effects, "%s tempo %s %s", temp, argv[i], argv[i+1]);
				Verbose("Option: tempo %s %s\n", argv[i], argv[i+1]);
				i++;
			}
			continue;
		}
		if (!strcmp(argv[i], "-pitch"))
		{
			i++;
			if ((i+1) < argc)
			{
				strcpy(temp, effects);
				sprintf(effects, "%s pitch %s %s", temp, argv[i], argv[i+1]);
				Verbose("Option: pitch %s %s\n", argv[i], argv[i+1]);
				i++;
			}
			continue;
		}
		if (!strcmp(argv[i], "-gain"))
		{
			i++;
			if (i < argc)
			{
				strcpy(temp, effects);
				sprintf(effects, "%s gain %s", temp, argv[i]);
				Verbose("Option: volume gain %sDb\n", argv[i]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-bass"))
		{
			i++;
			if (i < argc)
			{
				strcpy(temp, effects);
				sprintf(effects, "%s bass %s", temp, argv[i]);
				Verbose("Option: bass gain %sDb\n", argv[i]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-treble"))
		{
			i++;
			if (i < argc)
			{
				strcpy(temp, effects);
				sprintf(effects, "%s treble %s", temp, argv[i]);
				Verbose("Option: treble gain %sDb\n", argv[i]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-normalize"))
		{
			strcpy(temp, effects);
			sprintf(effects, "%s gain -n", temp);
			Verbose("Option: normalize volume\n");
			continue;
		}
		if (!strcmp(argv[i], "-reverb"))
		{
			strcpy(temp, effects);
			sprintf(effects, "%s reverb", temp);
			Verbose("Option: reverbance\n");
			continue;
		}
		if (!strcmp(argv[i], "-ir"))
		{
			i++;
			if (i < argc)
			{
				ir = atoi(argv[i]);
				Verbose("Option: input rate %ihz\n", ir);
			}
			continue;
		}
		// add to effect
		sprintf(effects, "%s %s", temp, argv[i]);
	}

	// get format
	if (!stricmp(format, "wav"))
		strcpy(outformat, "-t wav -e signed-integer");
	else if (!stricmp(format, "ogg"))
		strcpy(outformat, "-t ogg -C 7");
	else
	{
		strcpy(outformat, format);
		Verbose("Option: using custom format '%s'\n", format);
	}

	// input parms
	strcpy(informat, infileformat);
	if (ir)
	{
		strcpy(temp, informat);
		sprintf(informat, "%s -r %i", temp, ir);
	}

	// run SoX
	if (!SoX_DataToData(entry->data, entry->size, "--no-dither", informat, outformat, &outdata, &outsize, effects))
		Error("SoX error\n");
	SaveFile(outfile, outdata, outsize);
	qfree(outdata);
	Print("done.\n");
}

void BigFile_ExtractEntry(int argc, char **argv, FILE *bigfile, bigfileentry_t *entry, char *outfile)
{
	char filename[MAX_BLOODPATH], basename[MAX_BLOODPATH], format[512], last;
	qboolean with_solid, with_triggers, toggled_objects;
	bigfileheader_t *bigfileheader;
	rawblock_t *rawblock;
	byte *data;
	int i, size;

	// cannot extract empty files
	if (entry->size == 0)
		Error("Empty file\n");

	// additional parms
	with_solid = false;
	with_triggers = false;
	toggled_objects = false;
	ExtractFileExtension(outfile, format);
	for (i = 0; i < argc; i++)
	{
		if (!strcmp(argv[i], "-f"))
		{
			i++; 
			if (i < argc)
			{
				strlcpy(format, argv[i], sizeof(format));
				Verbose("Option: format '%s'\n", argv[i]);
			}
			continue;
		}
		if (!strcmp(argv[i], "-c"))
		{
			Verbose("Option: showing contents\n");
			with_solid = true;
			continue;
		}
		if (!strcmp(argv[i], "-t"))
		{
			Verbose("Option: showing triggers\n");
			with_triggers = true;
			continue;
		}
		if (!strcmp(argv[i], "-a"))
		{
			Verbose("Option: dynamic objects in alternative state\n");
			toggled_objects = true;
			continue;
		}
	}

	if (!format[0])
		Error("Format is not given\n");
	
	// raw extract (no conversion)
	if (!stricmp(format, "raw"))
	{
		// load file contents
		entry->data = qmalloc(entry->size);
		BigfileSeekContents(bigfile, entry->data, entry);
		// save file
		SaveFile(outfile, entry->data, entry->size);
		qfree(entry->data);
		entry->data = NULL;
		return;
	}

	// get outfile
	if (outfile == NULL)
		ExtractFileName(entry->name, filename); // pick automatic name
	else
	{
		last = outfile[strlen(outfile)-1];
		if (last != '/' && last != '\\') // full path is given
			strcpy(filename, outfile);
		else // only path is given
		{
			ExtractFileName(entry->name, basename);
			sprintf(filename, "%s%s", outfile, basename);
		}
	}

	// extract
	switch(entry->type)
	{
		case BIGENTRY_UNKNOWN:
			Error("unknown entry type, bad format '%s'\n", format);
			break;
		case BIGENTRY_TIM:
			// TIM extraction is simple
			if (!stricmp(format, "tga"))
			{
				DefaultExtension(filename, ".tga", sizeof(filename));
				Print("writing %s.\n", filename);
				TGAfromTIM(bigfile, entry, filename, false); 
			}
			else if (!stricmp(format, "tga24") || !format[0])
			{
				DefaultExtension(filename, ".tga", sizeof(filename));
				Print("writing %s.\n", filename);
				TGAfromTIM(bigfile, entry, filename, true); 
			}
			else Error("unknown format '%s'\n", format);
			break;
		case BIGENTRY_RAW_ADPCM:
			// load file contents
			entry->data = qmalloc(entry->size);
			BigfileSeekContents(bigfile, entry->data, entry);
			// process
			if (!stricmp(format, "wav") || !format[0])
			{
				DefaultExtension(filename, ".wav", sizeof(filename));
				BigFile_ExtractSound(argc, argv, outfile, entry, "-t ima -c 1", 11025, "wav");
			}
			else if (!stricmp(format, "ogg"))
			{
				DefaultExtension(filename, ".ogg", sizeof(filename));
				BigFile_ExtractSound(argc, argv, outfile, entry, "-t ima -c 1", 11025, "ogg");
			}
			else Error("unknown format '%s'\n", format);
			// close
			qfree(entry->data);
			entry->data = NULL;
			break;
		case BIGENTRY_RIFF_WAVE:
			// load file contents
			entry->data = qmalloc(entry->size);
			BigfileSeekContents(bigfile, entry->data, entry);
			// process
			if (!stricmp(format, "wav") || !format[0])
			{
				DefaultExtension(filename, ".wav", sizeof(filename));
				BigFile_ExtractSound(argc, argv, outfile, entry, "", 0, "wav");
			}
			else if (!stricmp(format, "ogg"))
			{
				DefaultExtension(filename, ".ogg", sizeof(filename));
				BigFile_ExtractSound(argc, argv, outfile, entry, "", 0, "ogg");
			}
			else Error("unknown format '%s'\n", format);
			// close
			qfree(entry->data);
			entry->data = NULL;
			break;
		case BIGENTRY_VAG:
			// load file contents
			entry->data = qmalloc(entry->size);
			BigfileSeekContents(bigfile, entry->data, entry);
			// unpack vag
			VAG_Unpack(entry->data, 64, entry->size, &data, &size);
			qfree(entry->data);
			entry->data = data;
			entry->size = size;
			// extract as normal sound then
			if (!stricmp(format, "wav") || !format[0])	
			{
				DefaultExtension(filename, ".wav", sizeof(filename));
				BigFile_ExtractSound(argc, argv, outfile, entry, "-t s16 -c 1", 11025, "wav");
			}
			else if (!stricmp(format, "ogg"))
			{
				DefaultExtension(filename, ".ogg", sizeof(filename));
				BigFile_ExtractSound(argc, argv, outfile, entry, "-t s16 -c 1", 11025, "ogg");
			}
			else Error("unknown format '%s'\n", format);
			// close
			qfree(entry->data);
			entry->data = NULL;
			break;
		case BIGENTRY_SPRITE:
			// read file contents and convert to rawblock, then pass to extraction func
			entry->data = qmalloc(entry->size);
			BigfileSeekContents(bigfile, entry->data, entry);
			rawblock = RawExtract(entry->data, entry->size, entry->rawinfo, false, false, RAW_TYPE_UNKNOWN);
			if (!stricmp(format, "tga") || !format[0])
			{
				DefaultExtension(filename, ".tga", sizeof(filename));
				BigFile_ExtractRawImage(argc, argv, outfile, entry, rawblock, "tga");
			}
			else if (!stricmp(format, "spr32"))
			{
				DefaultExtension(filename, ".spr32", sizeof(filename));
				BigFile_ExtractRawImage(argc, argv, outfile, entry, rawblock, "spr32");
			}
			else Error("unknown format '%s'\n", format);
			FreeRawBlock(rawblock);
			qfree(entry->data);
			entry->data = NULL;
			break;
		case BIGENTRY_TILEMAP:
			data = qmalloc(entry->size);
			size = entry->size;
			BigfileSeekContents(bigfile, data, entry);
			entry->data = LzDec(&entry->size, data, 0, size, true);
			// extract as TIM
			if (!stricmp(format, "tga"))
			{
				DefaultExtension(filename, ".tga", sizeof(filename));
				Print("writing %s.\n", filename);
				TGAfromTIM(bigfile, entry, filename, false); 
			}
			else if (!stricmp(format, "tga24") || !format[0])
			{
				DefaultExtension(filename, ".tga", sizeof(filename));
				Print("writing %s.\n", filename);
				TGAfromTIM(bigfile, entry, filename, true); 
			}
			else Error("unknown format '%s'\n", format);
			qfree(data);
			qfree(entry->data);
			entry->data = NULL;
			entry->size = size;
			break;
		case BIGENTRY_MAP:
			bigfileheader = ReadBigfileHeader(bigfile, false, false);
			entry->data = qmalloc(entry->size);
			BigfileSeekContents(bigfile, entry->data, entry);
			if (!stricmp(format, "tga"))
			{
				DefaultExtension(filename, ".tga", sizeof(filename));
				MapExtract(entry->name, entry->data, entry->size, filename, bigfileheader, bigfile, "", with_solid, with_triggers, toggled_objects, false, 0);
			}
			else Error("unknown format '%s'\n", format);
			qfree(entry->data);
			qfree(bigfileheader);
			entry->data = NULL;
			break;
		default:
			Error("bad entry type\n");
			break;
	}
}

// "-bigfile c:/pill.big -extract 0AD312F45 0AD312F45.tga"
int BigFile_Extract(int argc, char **argv)
{
	char outfile[MAX_BLOODPATH];
	unsigned int hash;
	bigfileentry_t *entry;
	FILE *f;

	// read source hash and out file
	if (argc < 2)
		Error("not enough parms");
	hash = BigfileEntryHashFromString(argv[0], true);
	strcpy(outfile, argv[1]);
	// open, get entry, scan, extract
	f = SafeOpen(bigfile, "rb");
	entry = ReadBigfileHeaderOneEntry(f, hash);
	if (entry == NULL)
		Error("Failed to find entry %.8X\n", hash);
	BigfileScanFiletype(f, entry, true, RAW_TYPE_UNKNOWN, true);
	BigFile_ExtractEntry(argc-2, argv+2, f, entry, outfile);
	fclose(f);
	return 0;
}

/*
==========================================================================================

  -bigfile -unpack

  unpack whole bigfile to a folder

==========================================================================================
*/

int BigFile_Unpack(int argc, char **argv)
{
	FILE *f, *f2;
	char savefile[MAX_BLOODPATH], dstdir[MAX_BLOODPATH];
	qboolean tim2tga, bpp16to24, nopaths, rawconvert, rawnoalign, hashnamesonly, map2tga, map_show_triggers, map_show_contents, map_toggled_objects;
	rawtype_t forcerawtype;
	bigfileheader_t *data;
	list_t *ixlist;
	int i, adpcmconvert, vagconvert;

	// parse commandline parms
	strcpy(dstdir, DEFAULT_PACKPATH);
	ixlist = NewList();
	tim2tga = false;
	map2tga = false;
	bpp16to24 = false;
	nopaths = false;
	adpcmconvert = 0;
	vagconvert = 0;
	rawconvert = false;
	forcerawtype = RAW_TYPE_UNKNOWN;
	rawnoalign = false;
	hashnamesonly = false;
	map2tga = false;
	map_show_triggers = false;
	map_show_contents = false;
	map_toggled_objects = false;
	if (argc > 0)
	{
		if (argv[0][0] != '-')
		{
			strcpy(dstdir, argv[0]);
			Verbose("Option: destination directory '%s'\n", dstdir);
		}
		for (i = 0; i < argc; i++)
		{
			if (!strcmp(argv[i], "-x"))
			{
				i++; 
				if (i < argc)
					ListAdd(ixlist, argv[i], false);
				Verbose("Option: exclude mask '%s'\n", argv[i]);
				continue;
			}
			if (!strcmp(argv[i], "-i"))
			{
				i++; 
				if (i < argc)
					ListAdd(ixlist, argv[i], true);
				Verbose("Option: include mask '%s'\n", argv[i]);
				continue;
			}
			if (!strcmp(argv[i], "-hashasnames"))
			{
				hashnamesonly = true;
				Verbose("Option: use pure hash names\n");
				continue;
			}
			if (!strcmp(argv[i], "-tim2tga"))
			{
				tim2tga = true;
				Verbose("Option: TIM->TGA conversion\n");
				continue;
			}
			if (!strcmp(argv[i], "-map2tga"))
			{
				map2tga = true;
				Verbose("Option: Map->TGA conversion\n");
				continue;
			}
			if (!strcmp(argv[i], "-mapcontents"))
			{
				Verbose("Option: Map - showing contents\n");
				map_show_contents = true;
				continue;
			}
			if (!strcmp(argv[i], "-maptriggers"))
			{
				Verbose("Option: Map - showing triggers\n");
				map_show_triggers = true;
				continue;
			}
			if (!strcmp(argv[i], "-maptoggled"))
			{
				Verbose("Option: Map - showing toggled state for animated objects\n");
				map_toggled_objects = true;
				continue;
			}
			if (!strcmp(argv[i], "-16to24"))
			{
				bpp16to24 = true;
				Verbose("Option: Targa compatibility mode (converting 16-bit to 24-bit)\n");
				continue;
			}
			if (!strcmp(argv[i], "-nopaths"))
			{
				nopaths = true;
				Verbose("Option: Disallow klist-set subpaths\n");
				continue;
			}
			if (!strcmp(argv[i], "-adpcm2wav"))
			{
				adpcmconvert = 1;
				Verbose("Option: ADPCM->WAV (native) conversion\n");
				continue;
			}
			if (!strcmp(argv[i], "-adpcm2pcm"))
			{
				adpcmconvert = 2;
				Verbose("Option: ADPCM->WAV (PCM) conversion\n");
				continue;
			}
			if (!strcmp(argv[i], "-adpcm2ogg"))
			{
				adpcmconvert = 3;
				Verbose("Option: ADPCM->OGG (Vorbis quality 5) conversion\n");
				continue;
			}
			if (!strcmp(argv[i], "-vag2wav"))
			{
				vagconvert = 2;
				Verbose("Option: VAG->WAV (PCM native) conversion\n");
				continue;
			}
			if (!strcmp(argv[i], "-vag2ogg"))
			{
				vagconvert = 3;
				Verbose("Option: VAG->OGG (Vorbis quality 5) conversion\n");
				continue;
			}
			if (!strcmp(argv[i], "-forcerawtype"))
			{
				i++;
				if (i < argc)
				{
					forcerawtype = ParseRawType(argv[i]);
					Verbose("Option: Guessing all raw images is %s\n", UnparseRawType(forcerawtype));
				}
				continue;
			}
			if (!strcmp(argv[i], "-raw2tga"))
			{
				rawconvert = true;
				Verbose("Option: Converting raw images to TGA\n");
				continue;
			}
			if (!strcmp(argv[i], "-rawnoalign"))
			{
				rawnoalign = true;
				Verbose("Option: Disable RAW images aligning\n");
				continue;
			}
			if (i != 0)
				Warning("unknown parameter '%s'",  argv[i]);
		}
	}

	// open file & load header
	f = SafeOpen(bigfile, "rb");
	data = ReadBigfileHeader(f, false, hashnamesonly);
	BigfileScanFiletypes(f, data, true, ixlist->items ? ixlist : NULL, forcerawtype);

	// export all files
	for (i = 0; i < (int)data->numentries; i++)
	{
		if (ixlist->items)
			if (!MatchIXList(&data->entries[i], ixlist, true, true))
				continue;
		Pacifier("unpacking entry %i of %i...", i + 1, data->numentries);
		BigFileUnpackEntry(data, f, &data->entries[i], dstdir, tim2tga, bpp16to24, nopaths, adpcmconvert, vagconvert, rawconvert, forcerawtype, rawnoalign, map2tga, map_show_contents, map_show_triggers, map_toggled_objects);
	}
	PacifierEnd();

	// write listfile
	sprintf(savefile, "%s/listfile.txt", dstdir);
	f2 = SafeOpenWrite(savefile);
	BigfileWriteListfile(f2, data);
	WriteClose(f2);
	Print("wrote %s\ndone.\n", savefile);

	fclose (f);
	return 0;
}

int BigFile_Pack(int argc, char **argv)
{
	FILE *f;
	bigfileheader_t *data;
	bigfileentry_t *entry;
	tim_image_t *tim;
	char savefile[MAX_BLOODPATH], basename[MAX_BLOODPATH], srcdir[MAX_BLOODPATH], ext[128];
	byte *contents;
	int i, k, size;

	// check parms
	strcpy(srcdir, DEFAULT_PACKPATH);
	if (argc > 0)
	{
		if (argv[0][0] != '-')
		{
			strcpy(srcdir, argv[0]);
			Verbose("Option: source directory '%s'\n", srcdir);
		}
		for (i = 0; i < argc; i++)
		{
			if (i != 0)
				Warning("unknown parameter '%s'",  argv[i]);
		}
	}

	// open listfile
	data = BigfileOpenListfile(srcdir);

	// open bigfile
	f = fopen(bigfile, "rb");
	if (f)
	{
		Verbose("%s already exists, overwriting\n", bigfile);
		fclose(f);
	}
	f = SafeOpenWrite(bigfile);

	// write entries count, write headers later
	SafeWrite(f, &data->numentries, 4);
	fseek(f, data->numentries*12, SEEK_CUR);

	// write files
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];

		if (entry->size == 0)
			continue; // skip null files

		Pacifier("writing entry %i of %i...", i + 1, data->numentries);

		ExtractFileExtension(entry->name, ext);
		// autoconverted TIM
		if (!strcmp(ext, "tga") && entry->type == BIGENTRY_TIM)
		{
			sprintf(savefile, "%s/%s", srcdir, entry->name);
			entry->data = TIM_LoadFromTarga(savefile, entry->timtype[0]);
			size = ((tim_image_t *)entry->data)->filelen;
			TIM_WriteToStream(entry->data, f);
			FreeTIM(entry->data);
			// add sublayers
			StripFileExtension(entry->name, basename);
			for (k = 1; k < entry->timlayers; k++)
			{
				sprintf(savefile, "%s/%s_sub%i.tga", srcdir, basename, k);
				tim = TIM_LoadFromTarga(savefile, entry->timtype[k]); 
				size += tim->filelen;
				TIM_WriteToStream(tim, f);
				FreeTIM(tim);
			}
			entry->size = size;
		}
		// autoconverted WAV/OGG
		else if ((!strcmp(ext, "wav") || !strcmp(ext, "ogg")) && entry->type == BIGENTRY_RAW_ADPCM)
		{
			sprintf(savefile, "%s/%s", srcdir, entry->name);
			if (!SoX_FileToData(savefile, "--no-dither", "", "-t ima -c 1", &size, &contents, ""))
				Error("unable to convert %s, SoX Error\n", entry->name);
			entry->data = contents;
			entry->size = size;
			// write
			SafeWrite(f, entry->data, entry->size);
			qfree(entry->data);
		}
		// just write
		else
		{
			sprintf(savefile, "%s/%s", srcdir, entry->name);
			entry->size = LoadFile(savefile, &entry->data);
			SafeWrite(f, entry->data, entry->size);
			qfree(entry->data);
		}
	}
	PacifierEnd();

	// write headers
	Verbose("Recalculating offsets...\n", bigfile);
	BigfileHeaderRecalcOffsets(data, 0);
	fseek(f, 4, SEEK_SET);
	for (i = 0; i < (int)data->numentries; i++)
	{
		entry = &data->entries[i];
		Pacifier("writing header %i of %i...", i + 1, data->numentries);
		SafeWrite(f, &entry->hash, 4);
		SafeWrite(f, &entry->size, 4);
		SafeWrite(f, &entry->offset, 4);
	}
	PacifierEnd();

	WriteClose(f);
	Print("done.\n");
	return 0;
}

/*
==========================================================================================

  Patch

==========================================================================================
*/

#define MAX_PATCHFILES 1000
#define PILL_BIG_BIGGEST_ENTRY 1024 * 1024 * 4

typedef enum
{
	PATCH_RAW,
	PATCH_SOUND2ADPCM,
	PATCH_TGA2TIM
}patchconv_t;

typedef struct
{
	// bigfile entry this file points to
	bigfileentry_t *entry;
	unsigned int hash;
	// file data
	byte *data;
	int datasize;
}patchfile_t;

#define chunksize (1024 * 1024 * 4)
unsigned char chunkdata[chunksize];
int BigFile_Patch(int argc, char **argv)
{
	char patchfile[MAX_BLOODPATH], outfile[MAX_BLOODPATH], entryname[MAX_BLOODPATH], convtype[1024], line[1024], *l;
	int i, num_patchfiles, linenum, linebytes, linebytes_total, progress[4], patchedbytes, patchsize_total, chunkbytes;
	patchfile_t patchfiles[MAX_PATCHFILES], *pfile;
	bigfileheader_t *bigfilehead;
	bigfileentry_t *entry;
	int entries_new = 0, entries_changesize = 0, entries_total = 0;
	qboolean overwriting;
	unsigned int num_entries, written_entries, ofs;
	float p;
	FILE *f, *bigf, *tmp;

	if (argc < 1)
		Error("not enough parms");
	// check parms
	strcpy(patchfile, argv[0]);
	strcpy(outfile, bigfile);
	progress[0] = 5;
	progress[1] = 40;
	progress[2] = 60;
	progress[3] = 100;
	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-outfile"))
		{
			i++; 
			if (i < argc)
				strcpy(outfile, argv[i]);
			Verbose("Option: output file '%s'\n", outfile);
			continue;
		}
		if (i != 0)
			Warning("unknown parameter '%s'",  argv[i]);
	}

	// first step - (0-5) preload bigfile header
	if (strcmp(bigfile, outfile))
	{
		overwriting = false;
		Verbose("Deriving patch destination (fast)\n", bigfile);
		bigf = SafeOpen(bigfile, "rb");
	}
	else
	{
		overwriting = true;
		Verbose("Overwriting patch destination (slow)\n", bigfile);
		bigf = SafeOpen(bigfile, "rb+");
		fseek(bigf, 0, SEEK_SET);
	}
	Verbose("Loading %s...\n", bigfile);
	bigfilehead = ReadBigfileHeader(bigf, false, false);
	for (i = 0; i < (int)bigfilehead->numentries; i++)
	{
		entry = &bigfilehead->entries[i];
		// show pacifier
		p = ((float)i / (float)bigfilehead->numentries) * progress[0];
		PercentPacifier("%i", (int)p);
	}

	// second step - load patch files
	Verbose("Loading patch file %s...\n", patchfile);
	linenum = 0;
	linebytes = 0;
	num_patchfiles = 0;
	patchsize_total = 0;
	entries_new = false;
	entries_changesize = false;
	f = SafeOpen(patchfile, "rb");
	linebytes_total = Q_filelength(f);
	while(fgets(line, 1024, f))
	{
		linebytes += strlen(line) + 1;
		linenum++;
		l = line;
		while(*l && (*l == ' ' || *l == '	'))
			l++;
		if (!l[0] || (l[0] == '/' && l[1] == '/')) // comment and null strings
			continue;
		if (!sscanf(l, "%s %s %s", &convtype, &entryname, &patchfile))
			Error("failed to read patchfile line %i", linenum);
		// single line
		if (strcmp(convtype, "RAW") && strcmp(convtype, "DEL") && strcmp(convtype, "WAV2ADPCM"))
		{
			strcpy(convtype, "RAW");
			strcpy(patchfile, l);
			// trim
			while(strlen(patchfile) && (patchfile[strlen(patchfile)-1] == '\n' || patchfile[strlen(patchfile)-1] == '\r' || patchfile[strlen(patchfile)-1] == ' '))
			{
				i = strlen(patchfile) - 1;
				patchfile[i] = 0;
			}
			// set internal name
			ExtractFileName(patchfile, entryname);
			// there are no .WAV files in BO, only .VAG
			ReplaceExtension(entryname, ".wav", ".vag", "");
		}
		// register patchfile and preload them
		if (num_patchfiles >= MAX_PATCHFILES)
			Error("MAX_PATCHFILES = %i exceeded, consider increase", MAX_PATCHFILES);
		// find entry for patchfile
		pfile = &patchfiles[num_patchfiles];
		pfile->hash = BigfileEntryHashFromString(entryname, true);
		if (!pfile->hash)
		{
			ExtractFileBase(entryname, convtype);
			pfile->hash = BigfileEntryHashFromString(convtype, true);
			if (!pfile->hash)
				Error("cannot resolve hash on line %i", linenum);
		}
		pfile->entry = BigfileGetEntry(bigfilehead, pfile->hash);
		// scan filetype for entry
		if (pfile->entry && pfile->entry->size)
			BigfileScanFiletype(bigf, pfile->entry, false, RAW_TYPE_UNKNOWN, false);
		//load patchfile
		if (!strcmp(convtype, "RAW"))
			pfile->datasize = LoadFile(patchfile, &pfile->data);
		else if (!strcmp(convtype, "DEL"))
			pfile->datasize = 0;
		else if (!strcmp(convtype, "WAV2ADPCM"))
		{
			if (!SoX_FileToData(patchfile, "--no-dither", "", "-t ima -c 1", &pfile->datasize, &pfile->data, ""))
				Error("unable to convert %s, SoX Error on line %i\n", patchfile, linenum);
		}
		else
			Error("bad patch filetype on line %i", linenum);
		if (!pfile->data)
			Error("patchfile has no data on line %i", linenum);
		patchsize_total += pfile->datasize;
		num_patchfiles++;
		// for optimal patching way
		if (!pfile->entry)
			entries_new++;
		else if (pfile->datasize != pfile->entry->size)
			entries_changesize++;
		// show pacifier
		p = progress[0] + ((float)linebytes / (float)linebytes_total) * (progress[1] - progress[0]);
		PercentPacifier("%i", (int)p);
	}
	fclose(f);

	if (!num_patchfiles)
		Error("nothing to patch");

	Verbose(" %i patch files\n", num_patchfiles);
	Verbose(" %i new\n", entries_new);
	Verbose(" %i changing size\n", entries_changesize);

	// most simple patch - if size is not changed
	patchedbytes = 0;
	if (!entries_new && !entries_changesize)
	{
		Verbose("Applying simple patch...\n");
		for (i = 0; i < num_patchfiles; i++)
		{
			pfile = &patchfiles[i];
			fseek(bigf, pfile->entry->offset, SEEK_SET);
			fwrite(pfile->data, pfile->datasize, 1, bigf);
			patchedbytes += pfile->datasize;
			// show pacifier
			p = progress[1] + ((float)patchedbytes / (float)patchsize_total) * (progress[3] - progress[1]);
			PercentPacifier("%i", (int)p);
		}
	}
	else
	{
		Verbose("Applying extended patch...\n");
		// copy out pillbig
		if (!overwriting)
		{
			tmp = bigf;
			bigf = SafeOpen(outfile, "wb");
		}
		else
		{
			Verbose("Buffering %s...\n", bigfile);
			fseek(bigf, 0, SEEK_SET);
			linebytes = 0;
			linebytes_total = Q_filelength(bigf);
			tmp = tmpfile();
			while(linebytes < linebytes_total)
			{
				chunkbytes = min(linebytes_total - linebytes, chunksize);
				fread(chunkdata, chunkbytes, 1, bigf);
				fwrite(chunkdata, chunkbytes, 1, tmp);
				linebytes += chunkbytes;
				// show pacifier
				p = progress[2] + ((float)linebytes / (float)linebytes_total) * (progress[3] - progress[2]);
				PercentPacifier("%i", (int)p);
			}
		}
		Verbose("Patching %s...\n", bigfile);
		// set new sizes
		for (i = 0; i < num_patchfiles; i++)
		{
			pfile = &patchfiles[i];
			if (pfile->entry)
			{
				pfile->entry->size = pfile->datasize;
				pfile->entry->data = pfile->data;
			}
		}
		// recalc offsets
		BigfileHeaderRecalcOffsets(bigfilehead, entries_new);
		// write new bigfile header
		num_entries = bigfilehead->numentries + entries_new;
		fseek(bigf, 0, SEEK_SET);
		SafeWrite(bigf, &num_entries, 4);
		for (i = 0; i < (int)bigfilehead->numentries; i++)
		{
			entry = &bigfilehead->entries[i];
			SafeWrite(bigf, &entry->hash, 4);
			SafeWrite(bigf, &entry->size, 4);
			SafeWrite(bigf, &entry->offset, 4);
			ofs = entry->size + entry->offset;
		}
		for (i = 0; i < num_patchfiles; i++)
		{
			pfile = &patchfiles[i];
			if (pfile->entry)
				continue;
			SafeWrite(bigf, &pfile->hash, 4);
			SafeWrite(bigf, &entry->size, 4);
			SafeWrite(bigf, &ofs, 4);
			ofs += entry->size;
		}
		// write entries
		written_entries = 0;
		for (i = 0; i < (int)bigfilehead->numentries; i++)
		{
			entry = &bigfilehead->entries[i];
			if (!entry->size)
				continue;
			if (entry->data)
				SafeWrite(bigf, entry->data, entry->size);
			else
			{
				if (entry->size > PILL_BIG_BIGGEST_ENTRY)
					Error("PILL_BIG_BIGGEST_ENTRY = %i exceeded, consider increasing", PILL_BIG_BIGGEST_ENTRY);
				if (fseek(tmp, (long int)entry->oldoffset, SEEK_SET))
					Error("error seeking for data on file %.8X", entry->hash);
				if (fread(chunkdata, entry->size, 1, tmp) < 1)
					Error("error reading data on file %.8X (%s)", entry->hash, strerror(errno));
				SafeWrite(bigf, chunkdata, entry->size);
			}
			written_entries++;
			// show pacifier
			if (overwriting)
				p = progress[2] + ((float)written_entries / (float)num_entries) * (progress[3] - progress[2]);
			else
				p = progress[1] + ((float)written_entries / (float)num_entries) * (progress[3] - progress[1]);
			PercentPacifier("%i", (int)p);

		}
		for (i = 0; i < num_patchfiles; i++)
		{
			pfile = &patchfiles[i];
			if (!pfile->datasize || pfile->entry)
				continue;
			SafeWrite(bigf, pfile->data, pfile->datasize);
			written_entries++;
			// show pacifier
			if (overwriting)
				p = progress[2] + ((float)written_entries / (float)num_entries) * (progress[3] - progress[2]);
			else
				p = progress[1] + ((float)written_entries / (float)num_entries) * (progress[3] - progress[1]);
			PercentPacifier("%i", (int)p);
		}
		Verbose("Ending patch...\n", bigfile);
		fclose(tmp);

	}
	fclose(bigf);
	PacifierEnd();

	// free patch files
	for (i = 0; i < num_patchfiles; i++)
	{
		pfile = &patchfiles[num_patchfiles];
		if (pfile->data)
			qfree(pfile->data);
	}

	Print("done.\n");

	return 0;

}

/*
==========================================================================================

  Main

==========================================================================================
*/

int BigFile_Main(int argc, char **argv)
{
	int i = 1, returncode = 0;
	char knownfiles[MAX_BLOODPATH], *c;

	Verbose("=== BigFile ===\n");
	if (i < 1)
		Error("not enough parms");

	// get input file
	c = argv[i];
	if (c[0] != '-')
	{
		strcpy(bigfile, c);
		i++;
	}
	else
		strcpy(bigfile, "pill.big");

	// check for special directives
	strcpy(knownfiles, "-");
	while(i < argc)
	{
		if (!strcmp(argv[i], "-klist"))
		{
			i++; 
			if (i < argc)
				strlcpy(knownfiles, argv[i], sizeof(knownfiles));
			i++; 
			continue;
		}
		break;
	}

	// load up knowledge base
	// FIXME: stupid code, rewrite
	if (knownfiles[0] == '-')
		bigklist = BigfileLoadKList("klist.txt", false);
	else
	{
		Print("Using custom known-files-list %s\n", knownfiles);
		bigklist = BigfileLoadKList(knownfiles, true);
	}

	// now we have to parse action
	if (argc <= i)
		Error("no action specified, try %s -help", progname);
	if (!strcmp(argv[i], "-list")) 
		returncode = BigFile_List(argc-i-1, argv+i+1);
	else if (!strcmp(argv[i], "-extract"))
		returncode = BigFile_Extract(argc-i-1, argv+i+1);
	else if (!strcmp(argv[i], "-unpack"))
		returncode = BigFile_Unpack(argc-i-1, argv+i+1);
	else if (!strcmp(argv[i], "-pack"))
		returncode = BigFile_Pack(argc-i-1, argv+i+1);
	else if (!strcmp(argv[i], "-patch"))
		returncode = BigFile_Patch(argc-i-1, argv+i+1);
	else
		Warning("unknown action %s", argv[i]);

	return returncode;
}