/*
    LZ4 HC - High Compression Mode of LZ4
    Copyright (C) 2011, Yann Collet.
    GPL v2 License

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

	You can contact the author at :
	- LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
	- LZ4-HC source repository : http://code.google.com/p/lz4hc/
*/


#if defined (__cplusplus)
extern "C" {
#endif


//****************************
// Simple Functions
//****************************

int LZ4_compressHC (char* source, char* dest, int isize);

/*
LZ4_compressHC :
	return : the number of bytes in compressed buffer dest
	note : destination buffer must be already allocated. 
		To avoid any problem, size it to handle worst cases situations (input data not compressible)
		Worst case size is : "inputsize + 0.4%", with "0.4%" being at least 8 bytes.
*/


//****************************
// Advanced Functions
//****************************



/* Note :
Decompression functions are provided within regular LZ4 source code (BSD license)
*/

#if defined (__cplusplus)
}
#endif
