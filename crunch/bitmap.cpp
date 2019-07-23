/*
 
 MIT License
 
 Copyright (c) 2017 Chevy Ray Johnston
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 
*/

#include "bitmap.hpp"
#include <iostream>
#define LODEPNG_NO_COMPILE_CPP
#include "lodepng.h"
#include <algorithm>
#include "hash.hpp"
#include <assert.h>
using namespace std;
Bitmap::Bitmap(Bitmap const& other)
	:name(other.name)
	,width(other.width)
	,height(other.height)
	,frameX(other.frameX)
	,frameY(other.frameY)
	,frameW(other.frameW)
	,frameH(other.frameH)
	,hashValue(other.hashValue)
{
	data = reinterpret_cast<uint32_t*>(
		calloc(width * height, sizeof(uint32_t)));
	CopyPixels(&other, 0, 0, 0);
}
Bitmap::Bitmap(const string& file, const string& name, bool premultiply, bool trim)
: name(name)
{
    //Load the png file
    unsigned char* pdata;
    unsigned int pw, ph;
    if (lodepng_decode32_file(&pdata, &pw, &ph, file.data()))
    {
        cerr << "failed to load png: " << file << endl;
        exit(EXIT_FAILURE);
    }
	int w = static_cast<int>(pw);
	int h = static_cast<int>(ph);
	uint32_t*const pixels = reinterpret_cast<uint32_t*>(pdata);
	postLoadProcess(file, premultiply, trim, pixels, w, h);
}
Bitmap::Bitmap(Bitmap const* bmSource, int sourceOffsetX, int sourceOffsetY,
	int frameWidth, int frameHeight,
	const string& name, bool premultiply, bool trim)
{
	this->name = name;
	// Create a new pixel data buffer and copy the desired subregion from 
	//	bmSource into it //
	uint32_t*const pixels = reinterpret_cast<uint32_t*>(
		calloc(frameWidth * frameHeight, sizeof(uint32_t)));
	for (int y = 0; y < frameHeight; y++)
	{
		for (int x = 0; x < frameWidth; x++)
		{
			const size_t i = y * frameWidth + x;
			const size_t iSrc =
				(sourceOffsetY + y) * bmSource->width + (sourceOffsetX + x);
			pixels[i] = bmSource->data[iSrc];
		}
	}
	// Then, run post load processes on the new pixel data 
	//	just like the other contructor //
	postLoadProcess(bmSource->name, premultiply, trim, pixels, frameWidth, frameHeight);
}
Bitmap::Bitmap(int width, int height)
: width(width), height(height)
{
    data = reinterpret_cast<uint32_t*>(calloc(width * height, sizeof(uint32_t)));
}

Bitmap::~Bitmap()
{
    free(data);
}

void Bitmap::SaveAs(const string& file)
{
    unsigned char* pdata = reinterpret_cast<unsigned char*>(data);
    unsigned int pw = static_cast<unsigned int>(width);
    unsigned int ph = static_cast<unsigned int>(height);
    if (lodepng_encode32_file(file.data(), pdata, pw, ph))
    {
        cout << "failed to save png: " << file << endl;
        exit(EXIT_FAILURE);
    }
}

void Bitmap::CopyPixels(const Bitmap* src, int tx, int ty, int edgePadSize)
{
	for (int y = -edgePadSize; y < src->height + edgePadSize; ++y)
	{
		for (int x = -edgePadSize; x < src->width + edgePadSize; ++x)
		{
			const int srcX = std::clamp(x, 0, src->width  - 1);
			const int srcY = std::clamp(y, 0, src->height - 1);
            data[(ty + y) * width + (tx + x)] = src->data[srcY * src->width + srcX];
		}
	}
}
void Bitmap::CopyPixelsRot(const Bitmap* src, int tx, int ty, int edgePadSize)
{
    int r = src->height - 1;
	for (int y = -edgePadSize; y < src->width + edgePadSize; ++y)
	{
		for (int x = -edgePadSize; x < src->height + edgePadSize; ++x)
		{
			const int srcX = std::clamp(x, 0, src->width  - 1);
			const int srcY = std::clamp(y, 0, src->height - 1);
            data[(ty + y) * width + (tx + x)] = src->data[(r - srcX) * src->width + srcY];
		}
	}
}

bool Bitmap::Equals(const Bitmap* other) const
{
    if (width == other->width && height == other->height)
        return memcmp(data, other->data, sizeof(uint32_t) * width * height) == 0;
    return false;
}
void Bitmap::postLoadProcess(string const& fileName, bool premultiply, 
	bool trim, uint32_t* pixels, int w, int h)
{
	//Premultiply all the pixels by their alpha
	if (premultiply)
	{
		int count = w * h;
		uint32_t c, a, r, g, b;
		float m;
		for (int i = 0; i < count; ++i)
		{
			c = pixels[i];
			a = c >> 24;
			m = static_cast<float>(a) / 255.0f;
			r = static_cast<uint32_t>((c & 0xff) * m);
			g = static_cast<uint32_t>(((c >> 8) & 0xff) * m);
			b = static_cast<uint32_t>(((c >> 16) & 0xff) * m);
			pixels[i] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}

	//TODO: skip if all corners contain opaque pixels?

	//Get pixel bounds
	int minX = w - 1;
	int minY = h - 1;
	int maxX = 0;
	int maxY = 0;
	if (trim)
	{
		uint32_t p;
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				p = pixels[y * w + x];
				if ((p >> 24) > 0)
				{
					minX = min(x, minX);
					minY = min(y, minY);
					maxX = max(x, maxX);
					maxY = max(y, maxY);
				}
			}
		}
		if (maxX < minX || maxY < minY)
		{
			minX = 0;
			minY = 0;
			maxX = w - 1;
			maxY = h - 1;
			cout << "image is completely transparent: " << fileName << endl;
		}
	}
	else
	{
		minX = 0;
		minY = 0;
		maxX = w - 1;
		maxY = h - 1;
	}

	//Calculate our trimmed size
	width = (maxX - minX) + 1;
	height = (maxY - minY) + 1;
	frameW = w;
	frameH = h;

	if (width == w && height == h)
	{
		//If we aren't trimmed, use the loaded image data
		frameX = 0;
		frameY = 0;
		data = pixels;
	}
	else
	{
		//Create the trimmed image data
		data = reinterpret_cast<uint32_t*>(calloc(width * height, sizeof(uint32_t)));
		frameX = -minX;
		frameY = -minY;

		//Copy trimmed pixels over to the trimmed pixel array
		for (int y = minY; y <= maxY; ++y)
			for (int x = minX; x <= maxX; ++x)
				data[(y - minY) * width + (x - minX)] = pixels[y * w + x];

		//Free the untrimmed pixels
		free(pixels);
	}

	//Generate a hash for the bitmap
	hashValue = 0;
	HashCombine(hashValue, static_cast<size_t>(width));
	HashCombine(hashValue, static_cast<size_t>(height));
	HashData(hashValue, reinterpret_cast<char*>(data), sizeof(uint32_t) * width * height);
}
void Bitmap::maskPixels(string const& newFileName)
{
	name = newFileName;
	const int numPixels = width * height;
	uint32_t p, a;
	for (int i = 0; i < numPixels; i++)
	{
		p = data[i];
		a = p >> 24;
		if (a == 0)
		{
			continue;
		}
		data[i] = 0xFFFFFFFF;
	}
	// re-hash this new bitmap //
	hashValue = 0;
	HashCombine(hashValue, static_cast<size_t>(width));
	HashCombine(hashValue, static_cast<size_t>(height));
	HashData(hashValue, reinterpret_cast<char*>(data), sizeof(uint32_t)* width* height);
}
void Bitmap::outlinePixels(string const& newFileName)
{
	name = newFileName;
	const int numPixels = width * height;
	uint32_t p, a, b, g, r;
	for (int i = 0; i < numPixels; i++)
	{
		p = data[i];
		a = p >> 24;
		b = (p >> 16) & 0xFF;
		g = (p >> 8 ) & 0xFF;
		r = p & 0xFF;
		if (a == 0 || r != 0 || g != 0 || b != 0)
		{
			data[i] = 0;
			continue;
		}
		data[i] = 0xFFFFFFFF;
	}
	// re-hash this new bitmap //
	hashValue = 0;
	HashCombine(hashValue, static_cast<size_t>(width));
	HashCombine(hashValue, static_cast<size_t>(height));
	HashData(hashValue, reinterpret_cast<char*>(data), sizeof(uint32_t)* width* height);
}
void Bitmap::swapPalette(string const& newFileName,
	vector<uint32_t> const& defaultPalette,
	vector<uint32_t> const& newPalette)
{
	assert(defaultPalette.size() == newPalette.size());
	name = newFileName;
	const int numPixels = width * height;
	uint32_t p, a;
	auto findColorIndex = [](uint32_t color,
		vector<uint32_t> const& palette)->size_t
	{
		// strip the alpha channel from the color because with respect to palettes,
		//	it doesn't matter.  All palette color data has zeroed out alpha channels //
		color &= 0x00FFFFFF;
		for (size_t c = 0; c < palette.size(); c++)
		{
			if (palette[c] == color)
			{
				return c;
			}
		}
		return palette.size();
	};
	for (int i = 0; i < numPixels; i++)
	{
		p = data[i];
		a = p >> 24;
		if (a == 0)
		{
			continue;
		}
		const size_t defaultPaletteIndex = findColorIndex(p, defaultPalette);
		assert(defaultPaletteIndex < defaultPalette.size());
		data[i] = (a << 24) | newPalette[defaultPaletteIndex];
	}
	// re-hash this new bitmap //
	hashValue = 0;
	HashCombine(hashValue, static_cast<size_t>(width));
	HashCombine(hashValue, static_cast<size_t>(height));
	HashData(hashValue, reinterpret_cast<char*>(data), sizeof(uint32_t)* width* height);
}