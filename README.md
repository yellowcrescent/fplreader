fplreader
=========

* Author: *J. Hipps*
* First working build: *14 Jan 2010*
* License: *LGPL Version 3*

# About

*fplreader* is a fast and light-weight [foobar2000](http://www.foobar2000.org) FPL playlist parser written in C with no dependencies other than the standard C library. It can compile and run on just about any system with a working GCC compiler or using MSVC, such as Linux, FreeBSD, Windows, and Mac OS X. Although the program is command-line driven, it is very easy to use. Usage examples and syntax is outlined below.

My primary reason for writing this program back in 2010 was so that I could create a web interface for browsing my music. However, I really got used to the way foobar2000 parsed the file metadata and enjoyed the speed at which it did as. Also, re-parsing tens of thousands of files isn't exactly quick, no matter how optimized your program becomes.

I hope you find this software useful, or are able to incorporate the code into your own project, or at the very least the FPL format becomes just a bit clearer for use in your own implementation. I only ask that you provide appropriate credit where due.

# FPL Format

After realizing that foobar2000's playlist format was unpublished and nobody at the time had managed to reverse-engineer its layout, I set about to do so on my own. After hours of hacking about with a hex editor and modifying and re-saving playlists to see how the resulting file changed, I managed to glean the overall structure and most of the fields which are stored by the player.

The overall structure uses a track index at the top of the file, followed by a string table containing detailed entries for the metadata and file attributes.


### Outline

* Layout
	* __16 bytes__ - **Magic signature**
		* `{ 0xE1, 0xA0, 0x9C, 0x91, 0xF8, 0x3C, 0x77, 0x42, 0x85, 0x2C, 0x3B, 0xCC, 0x14, 0x01, 0xD3, 0xF2 }`
	* __4 bytes__ - **Total size of `track index`** in bytes, as a 32-bit `unsigned int`, little-endian
	* __data_sz__ - **Track data area**, size determined by previous integer
	* __4 bytes__ - **Track count**, number of track indicies in the data structure
	* **EOF**

* Track data chunk
	* __4 bytes__ - `unsigned int unk1` - ??
	* __4 bytes__ - `unsigned int file_ofz` - filename string offset
	* __4 bytes__ - `unsigned int subsong` - subsong index value
	* __4 bytes__ - `unsigned int fsize` - filesize
	* __4 bytes__ - `unsigned int unk2` - ??
	* __4 bytes__ - `unsigned int unk3` - ??
	* __4 bytes__ - `unsigned int unk4` - ??
	* __8 bytes__ - `unsigned double int duration_dbl` - track duration, in seconds (program struct uses `char duration_dbl[8]`)
	* __4 bytes__ - `float rpg_album` - replay gain, album
	* __4 bytes__ - `float rpg_track` - replay gain, track
	* __4 bytes__ - `float rpk_album` - replay gain, album peak
	* __4 bytes__ - `float rpk_track` -replay gain, track peak
	* __4 bytes__ - `unsigned int keys_dex` - number of key/pointers that follow
	* __4 bytes__ - `unsigned int key_primary` - number of primary info keys
	* __4 bytes__ - `unsigned int key_second` - number of secondary info key combos
	* __4 bytes__ - `unsigned int key\_sec\_offset` - index of secondary key start

* Track attribute data
	* Key -> Value pairs, NULL terminated (see source listing for further details)

Further information can be gleaned from the source code to learn how the file is parsed, as the record length is not fixed, as foobar2000 can store an arbitrary number of metadata attributes for each file (although "arbitrary" is used somewhat losely, as it's likely limited in practice, and fplreader is limited to 512 key/value pairs per track)

# Binary downloads

[Win32 CLI application](http://jhipps.neoretro.net/fplreader/fplreader-0.10.zip)

# Compilation

On Linux, compilation

# Program Usage Syntax

<code>
fplreader **fpl\_file** *\[output\_file\]* *\[options\]*
</code>

* `fpl\_file` - Input FPL playlist filename
* `output\_file` - Output filename

* **Output format options**
	* `-sql_file <table|database.table>`
		* `table` - Table to which each row will be inserted
		* Or, `database.table` - Same as above, but also specifies database name

	* `-m3u` - Enables M3U extended playlist generation
	* `-m3u-noext` - Enables M3U filename-only playlist generation

	* `-csv` - Enable CSV Output mode

	* `-xml` - Enable XML Output mode (Rhythmbox-compatible schema)

* **Misc/Program Control options**
	* `-verbose` - Enable verbose output to stdout
	* `-windrive` - CSV: Output drive letter (Windows) to OPTIONAL field of CSV files
	* `-albonly` - CSV: Output artist/album information ONLY
	* `-fslash` - Transform backslash (\\) to forwardslash (/) in filename output string (useful if the files will be accessed via Linux/OSX via a network Samba share or similar)

# Usage Examples

## Convert playlist to SQL command listing
<code>
	fplreader *myplaylist.fpl* *output.sql* -sql_file *alltracks*
</code>
* *myplaylist.fpl* - Input playlist filename
* *output.sql* - Output SQL listing filename
* `-sql_file` flag enables SQL file output
* *alltracks* - Name of target table where the rows will be inserted

## Convert playlist to CSV file
<code>
	fplreader *myplaylist.fpl* *heavymetal.csv* -csv
</code>
* *myplaylist.fpl* - Input playlist filename
* *heavymetal.csv* - Output CSV filename
* `-csv` flag enables CSV output

## NULL Output
<code>
	fplreader *myplaylist.fpl* -verbose
</code>
* Specifying no output file or output type will use 'null' output mode, which is only useful when combined with the `-verbose` flag to diagnose problems with the program, but is also useful to get a feel for the format of the FPL file itself and the way it is parsed by the program. Verbose mode outputs the various file structures and offset indicies as it parses the file.



