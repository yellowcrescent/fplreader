/**
 **%%YCDOC*********************************************************************
 **%%vim: set ts=4 sw=4 noexpandtab syntax=c:
 * 
 * fplreader - foobar2000 FPL playlist parser
 * Parses foobar2000 FPL binary playlists
 *
 * This program is capable of reading a foobar2000 FPL binary playlist,
 * the outputting the result in various formats. Although the format is
 * not publicly available, I was able to reverse-engineer most of the
 * important portions of the data structures and overall format, which
 * can be gleaned through the comments and code in this source listing.
 *
 * This program has no dependencies and can easily be compiled for most
 * systems with no modification. Tested under GCC and MSVC for Linux,
 * Mac OS X, and Windows systems.
 *
 * TODO: This code definately needs some cleaning-up :( [19 Feb 2014]
 *
 * Copyright (c) 2011-2014 Jacob Hipps - tetrisfrog@gmail.com
 * http://jhipps.neoretro.net/
 *
 * Started: 14 Jan 2010
 * Updated: 08 Dec 2010
 * DocUpdt: 19 Feb 2014
 * Revision: r3
 *
 * @package		neoretro\fplreader
 * @subpackage	fplreader
 * @category	util
 * @fullpkg		neoretro.fplreader
 * @version		0.10.0
 * @author		Jacob Hipps - tetrisfrog@gmail.com
 * @copyright	Copyright (c) 2011,2014 Jacob Hipps/Neo-Retro Group
 * @license		GNU LGPLv3 - http://www.gnu.org/licenses/lgpl.html
 *
 * @depends		NONE
 *
 * @link		https://github.com/tetrisfrog/fplreader				fplreader Github page
 * @link		http://jhipps.neoretro.net/yccc/fplreader			YCCC - fplreader
 *
 *****************************************************************************/

#define _CRT_SECURE_NO_WARNINGS


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


#define FPL_MAGIC_SIG { 0xE1, 0xA0, 0x9C, 0x91, 0xF8, 0x3C, 0x77, 0x42, 0x85, 0x2C, 0x3B, 0xCC, 0x14, 0x01, 0xD3, 0xF2 }


typedef struct {
	unsigned int unk1;		// not sure??
	unsigned int file_ofz;	// filename string offset
	unsigned int subsong;	// subsong index value
	unsigned int fsize;		// filesize
	unsigned int unk2;		// ??
	unsigned int unk3;		// ??
	unsigned int unk4;		// ??
	char         duration_dbl[8]; // track duration data (converted later)
							// note: for some reason, using a double here will cause fread to read 12 bytes instead
							// of 8 (not to mention incorrectly interpreting the double in the first place, with
							// the most significant byte being null)...
							// possibly because of word alignment?? who knows...
	float        rpg_album;	// replay gain, album
	float        rpg_track;	// replay gain, track
	float		 rpk_album;	// replay gain, album peak
	float        rpk_track; // replay gain, track peak
	unsigned int keys_dex;	// number of key/pointers that follow
	unsigned int key_primary; // number of primary info keys
	unsigned int key_second;  // number of secondary info key combos
	unsigned int key_sec_offset; // index of secondary key start
} FPL_TRACK_CHUNK;


typedef struct {
	int key;
	char field_name[128];
	char value[1024];
} FPL_TRACK_ATTRIB;

// output types
enum {
	OUTMODE_NULL=0,			// no output
	OUTMODE_MYSQL=1,			// connects to a mysql database and generates INSERT statements
	OUTMODE_SQL_FILE=2,		// generates SQL output in the form of INSERT statements
	OUTMODE_M3U=3,			// generates an m3u extended playlist (includes length, artist, and track title)
	OUTMODE_M3U_NOEXT=4,	// traditional (non-extended) m3u playlist
	OUTMODE_CSV=5,			// CSV output dump
	OUTMODE_XML=6			// outputs XML in Rhythmbox-compatible format
};


// function declarations
int display_help(char *prgname);

char* get_attrib(char *astring, int listlen);
void escape_str(char *instr, char *outbuf, int outbufsz);
void xml_escape_str(char *instr, char *outbuf, int outbufsz);
int fpl_strcmpi(const char *s1, const char *s2); // replacement for strcmpi

int null_output(FILE *outfile, char *trackfile, int listlen);
int sqlfile_output(FILE *outfile, char *trackfile, int listlen);
int mysql_output(FILE *outfile, char *trackfile, int listlen);
int m3u_output(FILE *outfile, char *trackfile, int listlen);
int csv_output(FILE *outfile, char *trackfile, int listlen);
int m3u_noext_output(FILE *outfile, char *trackfile, int listlen);
int xml_output(FILE *outfile, char *trackfile, int listlen);


// output format lookup table
struct {
	char desc[32];
	int (*outfunc)(FILE* a,char* b,int c);
} out_lut[] = {
	{"null",null_output},
	{"mysql",mysql_output},
	{"sqlfile",sqlfile_output},
	{"m3u",m3u_output},
	{"m3u-noext",m3u_noext_output},
	{"csv",csv_output},
	{"xml",xml_output},
	{NULL,NULL}
};


// options
int  outmode = OUTMODE_NULL;
bool verbose = false;
bool option_windrive = false;
bool opt_alb_only = false;
bool opt_remap = false;
bool opt_fslash = false;

// opt_alb_only parameters
char last_aa[512];
char last_alb[512];


// option strings/ints
char mysql_host[128] = "localhost";
int  mysql_port = 3306;
char mysql_user[64] = "root";
char mysql_pass[64];
char sql_database[64] = "music_db";
char sql_table[64] = "fplreader";
char nullstring[2] = "\0";

// track data
FPL_TRACK_CHUNK		chunkrunner;
FPL_TRACK_ATTRIB	trackrunner[256];

// display syntax and version info
int display_help(char *prgname) {

	printf("Licensed under LGPLv3 - http://www.gnu.org/licenses/lgpl.txt\n\n");	

	printf("Syntax:\n\n");
	
	printf("%s fpl_file [output_file] [options]\n\n",prgname);
/*
	printf("-- mySQL output --\n");
	printf("   Connects to a mySQL database and inserts playlist data into a table\n\n");

	printf("-mysql <hostname|hostname:port> <database|database.table> user:password\n");
	printf("     -mysql          Enables mySQL direct query output\n");
	printf("   arguments: (all arguments required)\n");
	printf("     hostname	     mySQL server's IP or hostname\n");
	printf("or   hostname:port   mySQL server's IP/hostname and port number\n");
	printf("     database        mySQL database name (new tables will be created)\n");
	printf("or   database.table  mySQL database and table name\n");
	printf("                     (pre-existing tables will be used)\n");
	printf("     user:password   mySQL username and password\n\n");
*/	
	printf("-- SQL file output --\n");
	printf("   Writes to output_file a series of INSERT statements to be executed on a\n");
	printf("   SQL client\n\n");

	printf("-sql_file <table|database.table>\n");
	printf("      -sql_file      Enables SQL file output\n");
	printf("    arguments: (all arguments required)\n");
	printf("      table          Table to which each row will be inserted\n");
	printf("or    database.table Same as above, but also specifies database name\n\n");

	printf("-- M3U output (extended & traditional) --\n");
	printf("   Writes to output_file an M3U extended-type playlist, which also contains\n");
	printf("   track duration, title, and artist (in addition to filename)\n\n");

	printf("-m3u                 Enables M3U extended playlist generation\n");
	printf("-m3u-noext           Enables M3U filename-only playlist generation\n\n");

	printf("-- CSV output --\n");	
	printf("-csv                 Enable CSV Output mode\n\n");

	printf("-- XML output --\n");
	printf("-xml                 Enable XML Output mode (Rhythmbox-compatible schema)\n\n");

	printf("-- Misc options --\n\n");

	printf("-verbose             Enable verbose output to stdout\n");
	printf("-windrive            CSV: Output drive letter (Windows) to OPTIONAL field of CSV files\n");
	printf("-albonly             CSV: Output artist/album information ONLY\n");
	printf("-fslash              Transform backslash (\\) to forwardslash (/)\n");

	//printf("-remap <file>        Specify remap file. For more info, try -remap?\n");
	//printf("                     remap option allows reformatting filepath info\n");
	//printf("-remap?              Show help for remap function\n");
	
/*
	printf("-sql_spec spfile     spfile contains SQL table field list\n");
	printf("                     (type -specfile for more in-depth info)\n");
	printf("-specfile            Show specfile formatting syntax and help\n");
*/
	printf("\n\n\n");

	return 0;
}


int main(int argc, char **argv) {
	
	char filename[1024];
	char outfile[1024];
	int  fnstatus = 0;

	last_aa[0] = NULL;
	last_alb[0] = NULL;
	filename[0] = NULL;
	outfile[0]  = NULL;

	// print banner info

	printf("fplreader - built %s %s\n",__DATE__,__TIME__);
	printf("Copyright (c) 2010 Jacob Hipps (tetrisfrog@gmail.com)\n\n\n");


	// if no arguments are given, display syntax/version info
	if(argc < 2) {
		display_help(argv[0]);
		return 1;
	}

	// enumerate arguments and flags
	for(int i = 1; i < argc; i++) {
		// enable mysql output		
		if(!strcmp("-mysql",argv[i])) {
			outmode = OUTMODE_MYSQL;
			if(argc < (i+4)) {
				printf("error: incorrect syntax. switch -mysql requires 3 arguments!\n\n");
				display_help(argv[0]);
				return 200;
			} else if(fnstatus < 1) {
				printf("error: incorrect syntax. input filename must come before this option!\n\n");
				display_help(argv[0]);
				return 200;
			}

			// get arguments

			// hostname & port <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< fix
			if(argv[i+1][0] == '-') {
				printf("error: incorrect syntax. switch -mysql requires 3 arguments!\n\n");
				display_help(argv[0]);
				return 200;
			}
			strcpy(mysql_host,argv[i+1]);

			// database & table <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< fix
			if(argv[i+2][0] == '-') {
				printf("error: incorrect syntax. switch -mysql requires 3 arguments!\n\n");
				display_help(argv[0]);
				return 200;
			}
			strcpy(sql_database,argv[i+2]);

			// username & password <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< fix
			if(argv[i+3][0] == '-') {
				printf("error: incorrect syntax. switch -mysql requires 3 arguments!\n\n");
				display_help(argv[0]);
				return 200;
			}
			strcpy(mysql_user,argv[i+3]);

			i += 3; // account for this flag's 3 arguments

		// enable sql file output		
		} else if(!strcmp("-sql_file",argv[i])) {
			outmode = OUTMODE_SQL_FILE;
			if(argc < (i+2)) {
				printf("error: incorrect syntax. switch -sql_file requires an argument!\n\n");
				display_help(argv[0]);
				return 200;
			} else if(fnstatus < 2) {
				printf("error: incorrect syntax. input/output filenames must come before this option!\n\n");
				display_help(argv[0]);
				return 200;
			}

			// get argument

			// table / database & table <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< fix
			if(argv[i+1][0] == '-') {
				printf("error: incorrect syntax. switch -sql_file requires an argument!\n\n");
				display_help(argv[0]);
				return 200;
			}
			strcpy(sql_table,argv[i+1]);

			i++; // account for this flag's argument

		// enable extended M3U playlist output
		} else if(!strcmp("-m3u",argv[i])) {
			outmode = OUTMODE_M3U;

		// enable traditional M3U non-extended playlist output
		} else if(!strcmp("-m3u-noext",argv[i])) {
			outmode = OUTMODE_M3U_NOEXT;

		// enable CSV plain text output
		} else if(!strcmp("-csv",argv[i])) {
			outmode = OUTMODE_CSV;

                // enable XML output
                } else if(!strcmp("-xml",argv[i])) {
                        outmode = OUTMODE_XML;
		
		// transform all slashes to forwardslash
                } else if(!strcmp("-fslash",argv[i])) {
                        opt_fslash = true;

		// enable verbose program output on stdout
		} else if(!strcmp("-verbose",argv[i])) {
			verbose = true;

		} else if(!strcmp("-windrive",argv[i])) {
			option_windrive = true;

		} else if(!strcmp("-albonly",argv[i])) {
			opt_alb_only = true;

		// catch invalid arguments/flags so they arent copied to the input/output filenames
		} else if(argv[i][0] == '-' || !strcmp("/?",argv[i]) || !fpl_strcmpi("/h",argv[i])) {			
			display_help(argv[0]);
			return 200;
		} else {
			// input filename
			if(fnstatus == 0) {
				strcpy(filename,argv[i]);
				fnstatus++;

			// output filename
			} else if(fnstatus == 1) {
				strcpy(outfile,argv[i]);
				fnstatus++;

			// incorrect syntax error
			} else {
				printf("error: incorrect syntax! too many file names...\n\n");
				display_help(argv[0]);
				return 200;
			}
		}
	}

	if(verbose) {
		printf("Verbose output mode enabled.\n");
		if(opt_alb_only) printf("opt_alb_only enabled. Outputting only unique albums.\n");
		if(option_windrive) printf("option_windrive enabled. Outputting drive letter to option1 field.\n");
		printf("\n");
	}

	printf("Opening FPL file \"%s\"\n",filename);

	// open the fpl file

	FILE *fplfile = NULL;
	FILE *outtie  = NULL;

	if((fplfile = fopen(filename,"rb")) == NULL) {
		printf("Unable to open file for reading!\n\n");
		return 255;
	}

	if(outfile[0] != NULL) {
		printf("Opening output file \"%s\"\n",outfile);
		if((outtie = fopen(outfile,"w")) == NULL) {
			printf("Unable to open file for writing!\n\n");
			return 255;
		}
	}

	printf("Parsing & Writing...\n\n");
	
	// read 16-byte signature
	char magicsig[16];
	fread(magicsig,16,1,fplfile);

	// load primary data string into memory
	unsigned int data_sz;
	fread(&data_sz,4,1,fplfile);

	if(verbose) printf("size of primary data area = %i bytes\n",data_sz);

	if(verbose) printf("allocating memory for data area...\n");

	char *dataprime;

	if((dataprime = (char*)malloc(data_sz)) == NULL) {
		printf("error allocating memory for primary data area! (%i bytes)\n",data_sz);
		fclose(fplfile);
		return 254;
	}
	
	if(verbose) printf("Ok. Loading primary data area to memory...\n");

	// read in primary string to memory
	fread(dataprime,data_sz,1,fplfile);

	// read playlist count integer
	unsigned int plsize;
	fread(&plsize,4,1,fplfile);

	if(verbose) printf("trackrunner: There are %i items in the playlist.\n",plsize);

	// entering chunk reader loop...

	unsigned int		keyrunner[512];
	fpos_t				fploffset;
	int					krn_bytes = 0;
	double				duration_conv;
	int					real_keys;
	int					attrib_count = 0;
	int					trx_dex = 0;
	char				tdata_fname[1024];

	for(int i = 0; i < plsize && !feof(fplfile); i++) {
		fgetpos(fplfile, &fploffset);
		if(verbose) printf("trackrunner: Reading track info at: index < %i > / start offset < 0x%08X >...\n",i,fploffset);
		
		fread((void*)&chunkrunner,sizeof(FPL_TRACK_CHUNK),1,fplfile);
		fgetpos(fplfile, &fploffset);
		if(verbose) printf("\ttrackrunner: done. ending offset < 0x%08X >.\n",fploffset);

		// display chunkrunner results

		// tricky casting! MSVC won't let us do a char to double cast... so we'll show him who's boss...
		memcpy((void*)&duration_conv,chunkrunner.duration_dbl,8);


		if(verbose) {
			printf("\tchunkrunner:\n");
			printf("\t  unk1 = %i\n",chunkrunner.unk1);
			printf("\t  String table offset = %i\n",chunkrunner.file_ofz);
			printf("\t  Subsong index = %i\n",chunkrunner.subsong);
			printf("\t  Track filesize = %i bytes\n",chunkrunner.fsize);
			printf("\t  unk2 = %i  [ 0x%08X ]\n",chunkrunner.unk2,chunkrunner.unk2);
			printf("\t  unk3 = %i  [ 0x%08X ]\n",chunkrunner.unk3,chunkrunner.unk3);
			printf("\t  unk4 = %i  [ 0x%08X ]\n",chunkrunner.unk4,chunkrunner.unk4);
			printf("\t  Track duration = %0.02f seconds\n",duration_conv);
			printf("\t  ReplayGain, album = %0.02f dB\n",chunkrunner.rpg_album);
			printf("\t  ReplayGain, track = %0.02f dB\n",chunkrunner.rpg_track);
			printf("\t  ReplayGain, album peak = %0.02f dB\n",chunkrunner.rpk_album);
			printf("\t  ReplayGain, track peak = %0.02f dB\n",chunkrunner.rpk_track);
			printf("\t  --------------\n");
			printf("\t  keys_dex = %i, key_primary = %i, key_second = %i, key_sec_offset = %i\n\n",chunkrunner.keys_dex,chunkrunner.key_primary,chunkrunner.key_second,chunkrunner.key_sec_offset);
		}

		// attribute count is primary keys (key_primary) + secondary keys (key_second)
		attrib_count = chunkrunner.key_primary + chunkrunner.key_second;
		if(verbose) printf("\ttrackrunner determined this track has %i attribute fields.\n",attrib_count);
		
		// keys_dex sanity check
		if(chunkrunner.keys_dex > 512) {
			printf("\n\n\n>>>> ERROR: keys_dex > 512 (keys_dex = %i). Offset problem???\n",chunkrunner.keys_dex);
			return 250;
		}

		// read in key values from file
		fgetpos(fplfile, &fploffset);

		// since we've already read 3 of the "keys" (key_primary,key_second, and key_sec_offset), we subtract 3
		real_keys = chunkrunner.keys_dex - 3;

		if(verbose) printf("\tkeyrunner: reading %i (adjusted) values, starting offset = 0x%08X\n",real_keys,fploffset);
		fread((void*)&keyrunner,sizeof(unsigned int),real_keys,fplfile);
		fgetpos(fplfile, &fploffset);
		if(verbose) printf("\tkeyrunner: ending offset = 0x%08X\n",fploffset);

		// list key values

		/*
		if(verbose) {
			printf("\tkeyrunner:\n");
			for(int ii = 0; ii < real_keys; ii++) {
				printf("\t  key(%i) = %i\n",ii,keyrunner[ii]);
			}
			printf("\tkeyrunner done.\n");
		}
		*/

		// enumerate track data from keyrunner
		trx_dex = 0;  // reset trackrunner indexer

		// Enumerate primary keys, which contain a key_value->field_name pair (hence, the x2 multiplier).
		// After all the key_value->field_name pairs is a list of values which is preceeded by the
		// key_value which is equal to key_primary's value
		for(int ii = 0; ii < (chunkrunner.key_primary * 2); ii += 2) {			
			// key value
			trackrunner[trx_dex].key = keyrunner[ii];
			// field name
			strcpy(trackrunner[trx_dex].field_name,(char*)(dataprime + keyrunner[1+ii]));			
			// value
			//strcpy(trackrunner[trx_dex].value,(char*)(dataprime + keyrunner[1+trx_dex+(chunkrunner.key_primary * 2)]));
			strcpy(trackrunner[trx_dex].value,(char*)(dataprime + keyrunner[1+trackrunner[trx_dex].key+(chunkrunner.key_primary * 2)]));
			trx_dex++;
		}

		// enumerate secondary keys, which are field_name->value pairs, with NO key_value, as they are usually
		// additional data that is not used as often
		for(int ii = 0; ii < (chunkrunner.key_second * 2); ii += 2) {
			// set the key value as -1 to represent UNDEFINED
			trackrunner[trx_dex].key = -1; 
			// field name
			strcpy(trackrunner[trx_dex].field_name,(char*)(dataprime + keyrunner[ii+chunkrunner.key_sec_offset]));
			// value
			strcpy(trackrunner[trx_dex].value,(char*)(dataprime + keyrunner[1+ii+chunkrunner.key_sec_offset]));
			trx_dex++;
		}

		if(verbose) printf("\ttrackrunner enumerated %i attributes (expected %i)\n",trx_dex,attrib_count);


		// display data

		strcpy(tdata_fname,(char*)(dataprime + chunkrunner.file_ofz)); // get filename string


		if(verbose) {
			printf("\ttrackrunner discovered track data!\n");
			printf("\t  Track filename = \"%s\"\n",tdata_fname);		
			
			for(int ii = 0; ii < trx_dex; ii++) {
				printf("\t  \"%s\" (key = %i) = \"%s\"\n",trackrunner[ii].field_name,trackrunner[ii].key,trackrunner[ii].value);
			}
		}


		if(verbose) printf("\ttrackrunner: calling %s output function...\n",out_lut[outmode].desc);
		out_lut[outmode].outfunc(outtie,tdata_fname,trx_dex);
		if(verbose) printf("\t <<< Finished for this track!\n\n\n");
	}

	// perform footer writing, if needed
	out_lut[outmode].outfunc(outtie,(char*)NULL,-1);

	fclose(fplfile);
	if(outtie) fclose(outtie);

	printf("Complete!\n\n\n");

	return 0;
}

// quick-and-dirty replacement for strcmpi
int fpl_strcmpi(const char *s1, const char *s2) {

	// sanity check (make sure ptrs != null)
	if(!(s1 && s2)) return -1;

	// get str lengths
	int sl1 = strlen(s1);
	int sl2 = strlen(s2);

	// strings don't match in length
	if(sl1 != sl2) return 1;

	int i;
	int status = 0;

	for(i=0;i<sl1;i++) {
		if(tolower(s1[i]) != tolower(s2[i])) {
			status = 1;
			break;
		}
	}

	return status;
}


char* get_attrib(char *astring, int listlen) {
	for(int i = 0; i < listlen; i++) {
		if(!fpl_strcmpi(astring,trackrunner[i].field_name)) {
			return trackrunner[i].value;
		}
	}
	return nullstring;
}

void escape_str(char *instr, char *outstr, int outbufsz) {

	if(instr == NULL) return;

	int strsz = strlen(instr);
	int white = 0;
	int curzz = 0;
	char cc;

	for(int i = 0; i < strsz; i++) {
		cc = instr[i];
		switch(cc) {
			case '\"':
			case '\'':
			case '\\':
				white = 0;
				if(opt_fslash) {
					outstr[curzz] = '/';
					curzz++;
				} else {
					outstr[curzz] = '\\';
					outstr[curzz+1] = cc;
					curzz += 2;
				}
				break;
			case '\t':
			case '\n':
			case '\r':
			case ' ':
				white++;
				
				if(i == (white - 1)) break;   // trim leading whitespace
			    else if(white > 1) break;	  // remove double-spacing and padding
				else {						  // allow single spaces to remain
					outstr[curzz] = ' ';
					curzz++;
				}
				break;
			default:
				white = 0;
				outstr[curzz] = cc;
				curzz++;
				break;
		}
		if(curzz >= outbufsz) break;
	}

	outstr[curzz] = NULL;

	return;
}


void xml_escape_str(char *instr, char *outstr, int outbufsz) {

        if(instr == NULL) return;

        int strsz = strlen(instr);
        int white = 0;
        int curzz = 0;
        char cc;

        for(int i = 0; i < strsz; i++) {
                cc = instr[i];
                switch(cc) {
                        case ' ':
                                white = 0;
                                outstr[curzz] = '%';
                                outstr[curzz+1] = '2';
				outstr[curzz+2] = '0';
                                curzz += 3;
                                break;
                        case '\\':
				white = 0;
				outstr[curzz] = '/';
				curzz++;
                        	break;
                        default:
                                white = 0;
                                outstr[curzz] = cc;
                                curzz++;
                                break;
                }
                if(curzz >= outbufsz) break;
        }

        outstr[curzz] = NULL;

        return;
}



int null_output(FILE *outfile, char *trackfile, int listlen) {

	return 0;
}

int csv_output(FILE *outfile, char *trackfile, int listlen) {

	static bool headerwrite = false;

	double durationdub;
	unsigned int i_bitrate;

	char t_trackfile[1024];
	char t_title[512];
	char t_artist[512];
	char t_album_artist[512];
	char t_album[512];
	char t_tracknumber[64];
	char t_genre[512];
	char t_date[64];
	char t_codec[64];
	char t_codec_profile[64];
	char option1[256];

	int t_tracknum_int = -1;

	// write header
	if(!headerwrite) {
		fprintf(outfile,"filename, title, artist, album_artist, album, tracknum, genre, year, duration, bitrate, codec, codec_profile, filesize, option1\n");
		headerwrite = true;
	}

	// write footer (if needed)
	if(listlen == -1) {
		return 150;
	}

	escape_str(trackfile,t_trackfile,1024);
	escape_str(get_attrib("title",listlen),t_title,512);
	escape_str(get_attrib("artist",listlen),t_artist,512);
	escape_str(get_attrib("album artist",listlen),t_album_artist,512);
	escape_str(get_attrib("album",listlen),t_album,512);
	if(strlen(t_album_artist) < 3) {
		strcpy(t_album_artist,t_artist);
	}

	if(opt_alb_only) {
		if(!strcmp(last_aa,t_album_artist) && !strcmp(last_alb,t_album)) return 200;	// output rows with unique album/artist only
	}
	strcpy(last_aa,t_album_artist);
	strcpy(last_alb,t_album);


	/*
	if(!sscanf(get_attrib("tracknumber",listlen),"%*2i.%i",&t_tracknum_int)) {
		if(!sscanf(get_attrib("tracknumber",listlen),"%*1i%2i",&t_tracknum_int)){
			if(!sscanf(get_attrib("tracknumber",listlen),"%*2i/%2i",&t_tracknum_int)) {
				sscanf(get_attrib("tracknumber",listlen),"%2i",&t_tracknum_int);
			}
		}
	}
	*/

	if(t_tracknum_int != -1) {
		sprintf(t_tracknumber,"%i",t_tracknum_int);
	} else {
		strcpy(t_tracknumber,get_attrib("tracknumber",listlen));
	}

	// if option_windrive is enabled, put the drive letter into option1
	if(option_windrive) {
		option1[0] = toupper(t_trackfile[7]);
		option1[1] = NULL;
	} else {
		option1[0] = NULL;
	}

	escape_str(get_attrib("genre",listlen),t_genre,512);
	escape_str(get_attrib("date",listlen),t_date,64);
	escape_str(get_attrib("codec",listlen),t_codec,64);
	escape_str(get_attrib("codec_profile",listlen),t_codec_profile,64);

	// get duration
	memcpy((void*)&durationdub,chunkrunner.duration_dbl,8);

	// filename, title, artist, album_artist, album, tracknum, genre, year, duration, bitrate, codec, codec_profile, filesize, option1

	i_bitrate = atoi(get_attrib("bitrate",listlen));

	fprintf(outfile,"\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%0.02f,%i,\"%s\",\"%s\",%i,\"%s\"\n" ,
						t_trackfile,								// filex
						t_title,									// titlex
						t_artist,									// artist
						t_album_artist,								// album_artist
						t_album,									// album
						t_tracknumber,								// tracknum
						t_genre,									// genre
						t_date,										// year
						durationdub,								// duration (in seconds)
						i_bitrate,									// bitrate
						t_codec,									// codec
						t_codec_profile,							// codec_profile
						chunkrunner.fsize,							// filesize
						option1										// optional parameter
						
			);


	return 0;
}

/*

XML, Rhythmbox-compatible schema

added 12.08.2010 - jacob

*/

int xml_output(FILE *outfile, char *trackfile, int listlen) {

	static bool headerwrite = false;

        double durationdub;
        unsigned int i_bitrate;

        char t_trackfile[1024];
        char t_title[512];
        char t_artist[512];
        char t_album_artist[512];
        char t_album[512];
        char t_tracknumber[64];
        char t_genre[512];
        char t_date[64];
        char t_codec[64];
        char t_codec_profile[64];
        //char option1[256];

        int t_tracknum_int = -1;
	int i_unixdate     = 0;

        // write header
        if(!headerwrite) {
		fprintf(outfile,"<?xml version=\"1.0\" standalone=\"yes\"?>\n");
		fprintf(outfile,"<rhythmdb version=\"1.7\">\n");
                headerwrite = true;
        }

	// write footer
	if(listlen == -1) {
		fprintf(outfile,"</rhythmdb>\n");
		return 150;
	}

        xml_escape_str(trackfile,t_trackfile,1024);
        strcpy(t_title,		get_attrib("title",listlen));
        strcpy(t_artist,	get_attrib("artist",listlen));
        strcpy(t_album_artist,	get_attrib("album artist",listlen));
        strcpy(t_album,		get_attrib("album",listlen));
	strcpy(t_genre,		get_attrib("genre",listlen));
	strcpy(t_date,		get_attrib("date",listlen));
	strcpy(t_codec,		get_attrib("codec",listlen));
	strcpy(t_codec_profile,	get_attrib("codec_profile",listlen));        

	if(strlen(t_album_artist) < 3) {
                strcpy(t_album_artist,t_artist);
        }

        // get duration
        memcpy((void*)&durationdub,chunkrunner.duration_dbl,8);

	// format tracknumber
        if(t_tracknum_int != -1) {
                sprintf(t_tracknumber,"%i",t_tracknum_int);
        } else {
                strcpy(t_tracknumber,get_attrib("tracknumber",listlen));
        }

	// year to unix timestamp conversion (needs work -- TODO)
	int tsy = atoi(t_date);
	if(tsy > 1900 && tsy < 2100) {
		i_unixdate = ((tsy - 1970) * 31556926) + 1;
	} else {
		i_unixdate = 0;
	}

        // filename, title, artist, album_artist, album, tracknum, genre, year, duration, bitrate, codec, codec_profile, filesize, option1

        i_bitrate = atoi(get_attrib("bitrate",listlen));

	// rhythmbox does not completely support album_artist tags
	// but has something similar called artist-sort
	// so for compatibility both tags will be written
	fprintf(outfile,"  <entry type=\"song\">\n");
	fprintf(outfile,"    <title>%s</title>\n",t_title);
	fprintf(outfile,"    <genre>%s</genre>\n",t_genre);
	//fprintf(outfile,"    <album-artist>%s</album-artist>\n",t_album_artist);
	//fprintf(outfile,"    <artist-sort>%s</artist-sort>\n",t_album_artist);
	fprintf(outfile,"    <artist>%s</artist>\n",t_artist);
	fprintf(outfile,"    <album>%s</album>\n",t_album);
	fprintf(outfile,"    <track-number>%s</track-number>\n",t_tracknumber);
	fprintf(outfile,"    <duration>%.0f</duration>\n",durationdub);
	fprintf(outfile,"    <file-size>%i</file-size>\n",chunkrunner.fsize);
	fprintf(outfile,"    <location>%s</location>\n",t_trackfile);
	fprintf(outfile,"    <mountpoint>file://%c%c</mountpoint>\n",t_trackfile[7],t_trackfile[8]);
	fprintf(outfile,"    <mtime>1269409449</mtime>\n");
	fprintf(outfile,"    <last-seen>1291856711</last-seen>\n");
	fprintf(outfile,"    <bitrate>%i</bitrate>\n",i_bitrate);
	//fprintf(outfile,"    <date>%i</date>\n",i_unixdate);
	fprintf(outfile,"    <date>0</date>\n");
	fprintf(outfile,"    <mimetype>application/x-id3</mimetype>\n");
	// additional fields (might not be used by rhythmbox)
	//fprintf(outfile,"\t\t<codec>%s</codec>\n",t_codec);
	//fprintf(outfile,"\t\t<codec-profile>%s</codec-profile>\n",t_codec_profile);
	fprintf(outfile,"  </entry>\n");

	return 0;
}


int sqlfile_output(FILE *outfile, char *trackfile, int listlen) {

	double durationdub;
	unsigned int i_bitrate;

	char t_trackfile[1024];
	char t_title[512];
	char t_artist[512];
	char t_album_artist[512];
	char t_album[512];
	char t_tracknumber[64];
	char t_genre[512];
	char t_date[64];
	char t_codec[64];
	char t_codec_profile[64];
	int t_tracknum_int = -1;

	// footer callback... we dont need this for SQL file output.. ignore it
	if(listlen == -1) return 150;

	escape_str(trackfile,t_trackfile,1024);
	escape_str(get_attrib("title",listlen),t_title,512);
	escape_str(get_attrib("artist",listlen),t_artist,512);
	escape_str(get_attrib("album artist",listlen),t_album_artist,512);
	escape_str(get_attrib("album",listlen),t_album,512);
	if(strlen(t_album_artist) < 3) {
		strcpy(t_album_artist,t_artist);
	}

	/*
	if(!sscanf(get_attrib("tracknumber",listlen),"%*2i.%i",&t_tracknum_int)) {
		if(!sscanf(get_attrib("tracknumber",listlen),"%*1i%2i",&t_tracknum_int)){
			if(!sscanf(get_attrib("tracknumber",listlen),"%*2i/%2i",&t_tracknum_int)) {
				sscanf(get_attrib("tracknumber",listlen),"%2i",&t_tracknum_int);
			}
		}
	}
	*/

	if(t_tracknum_int != -1) {
		sprintf(t_tracknumber,"%i",t_tracknum_int);
	} else {
		strcpy(t_tracknumber,get_attrib("tracknumber",listlen));
	}

	escape_str(get_attrib("genre",listlen),t_genre,512);
	escape_str(get_attrib("date",listlen),t_date,64);
	escape_str(get_attrib("codec",listlen),t_codec,64);
	escape_str(get_attrib("codec_profile",listlen),t_codec_profile,64);

	// get duration
	memcpy((void*)&durationdub,chunkrunner.duration_dbl,8);

	// key, filename, title, artist, album artist, tracknum, genre, year, duration, bitrate,

	i_bitrate = atoi(get_attrib("bitrate",listlen));

	fprintf(outfile,"INSERT INTO %s VALUES(%i,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%0.02f,%i,\"%s\",\"%s\",%i);\n\r" ,
						sql_table,									// mysql
						0,											// key (auto-generated by mySQL)
						t_trackfile,								// filex
						t_title,									// titlex
						t_artist,									// artist
						t_album_artist,								// album_artist
						t_album,									// album
						t_tracknumber,								// tracknum
						t_genre,									// genre
						t_date,										// year
						durationdub,								// duration (in seconds)
						i_bitrate,									// bitrate
						t_codec,									// codec
						t_codec_profile,							// codec_profile
						chunkrunner.fsize							// filesize
						
			);


	return 0;
}



int mysql_output(FILE *outfile, char *trackfile, int listlen) {



	return 0;
}

int m3u_output(FILE *outfile, char *trackfile, int listlen) {



	return 0;
}

int m3u_noext_output(FILE *outfile, char *trackfile, int listlen) {



	return 0;
}
