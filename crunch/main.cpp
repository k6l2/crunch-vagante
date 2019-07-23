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
 
 crunch - command line texture packer
 ====================================
 
 usage:
    crunch [OUTPUT] [INPUT1,INPUT2,INPUT3...] [OPTIONS...]
 
 example:
    crunch bin/atlases/atlas assets/characters,assets/tiles -p -t -v -u -r
 
 options:
    -d  --default           use default settings (-x -p -t -u)
    -x  --xml               saves the atlas data as a .xml file
    -b  --binary            saves the atlas data as a .bin file
    -j  --json              saves the atlas data as a .json file
    -p  --premultiply       premultiplies the pixels of the bitmaps by their alpha channel
    -t  --trim              trims excess transparency off the bitmaps
    -v  --verbose           print to the debug console as the packer works
    -f  --force             ignore the hash, forcing the packer to repack
    -u  --unique            remove duplicate bitmaps from the atlas
    -r  --rotate            enabled rotating bitmaps 90 degrees clockwise when packing
    -s# --size#             max atlas size (# can be 4096, 2048, 1024, 512, 256, 128, or 64)
    -p# --pad#              padding between images (# can be from 0 to 16)
 
 binary format:
    [int16] num_textures (below block is repeated this many times)
        [string] name
        [int16] num_images (below block is repeated this many times)
            [string] img_name
            [int16] img_x
            [int16] img_y
            [int16] img_width
            [int16] img_height
            [int16] img_frame_x         (if --trim enabled)
            [int16] img_frame_y         (if --trim enabled)
            [int16] img_frame_width     (if --trim enabled)
            [int16] img_frame_height    (if --trim enabled)
            [byte] img_rotated          (if --rotate enabled)
 */

#include <iostream>
#include <fstream>
#include <streambuf>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "tinydir.h"
#include "bitmap.hpp"
#include "packer.hpp"
#include "binary.hpp"
#include "hash.hpp"
#include "str.hpp"
#include <rapidjson/document.h>
#include <filesystem>
namespace fs = std::filesystem;
using namespace std;

static int optSize;
static int optPadding;
static bool optXml;
static bool optBinary;
static bool optJson;
static bool optPremultiply;
static bool optTrim;
static bool optVerbose;
static bool optForce;
static bool optUnique;
static bool optRotate;

static void SplitFileName(const string& path, string* dir, string* name, string* ext)
{
    size_t si = path.rfind('/') + 1;
    if (si == string::npos)
        si = 0;
    size_t di = path.rfind('.');
    if (dir != nullptr)
    {
        if (si > 0)
            *dir = path.substr(0, si);
        else
            *dir = "";
    }
    if (name != nullptr)
    {
        if (di != string::npos)
            *name = path.substr(si, di - si);
        else
            *name = path.substr(si);
    }
    if (ext != nullptr)
    {
        if (di != string::npos)
            *ext = path.substr(di);
        else
            *ext = "";
    }
}

static string GetFileName(const string& path)
{
    string name;
    SplitFileName(path, nullptr, &name, nullptr);
    return name;
}

static void loadBitmap(const string& prefix, const string& path, vector<Bitmap*>& outBitmaps)
{
    if (optVerbose)
        cout << "Loading bitmap: '" << path << "'...";
	outBitmaps.push_back(new Bitmap(path, prefix + GetFileName(path), optPremultiply, optTrim));
	if (optVerbose)
		cout << "DONE!\n";
}

static void LoadBitmaps(const string& root, const string& prefix, vector<Bitmap*>& outBitmaps)
{
    static string dot1 = ".";
    static string dot2 = "..";
    tinydir_dir dir;
    tinydir_open(&dir, StrToPath(root).data());
    while (dir.has_next)
    {
        tinydir_file file;
        tinydir_readfile(&dir, &file);
        if (file.is_dir)
        {
            if (dot1 != PathToStr(file.name) && dot2 != PathToStr(file.name))
                LoadBitmaps(PathToStr(file.path), prefix + PathToStr(file.name) + "/", outBitmaps);
        }
        else if (PathToStr(file.extension) == "png")
            loadBitmap(prefix, PathToStr(file.path), outBitmaps);
        tinydir_next(&dir);
    }
    tinydir_close(&dir);
}

static void RemoveFile(string file)
{
    remove(file.data());
}

static int GetPackSize(const string& str)
{
    if (str == "4096")
        return 4096;
    if (str == "2048")
        return 2048;
    if (str == "1024")
        return 1024;
    if (str == "512")
        return 512;
    if (str == "256")
        return 256;
    if (str == "128")
        return 128;
    if (str == "64")
        return 64;
    cerr << "invalid size: " << str << endl;
    exit(EXIT_FAILURE);
    return 0;
}

static int GetPadding(const string& str)
{
    for (int i = 0; i <= 16; ++i)
        if (str == to_string(i))
            return i;
    cerr << "invalid padding value: " << str << endl;
    exit(EXIT_FAILURE);
    return 1;
}
struct Palette
{
	string name;
	vector<uint32_t> colors;
};
struct PaletteGroup
{
	vector<string> textureNames;
	vector<Palette> palettes;
};
int main(int argc, const char* argv[])
{
	vector<Bitmap*> bitmaps;
	vector<Packer*> packers;
	vector<PaletteGroup> paletteGroups;
///    //Print out passed arguments
///    for (int i = 0; i < argc; ++i)
///        cout << argv[i] << ' ';
///    cout << "\n";
    
    if (argc < 5)
    {
        cerr << "invalid input, expected: \"crunch [OUTPUT PREFIX] [INPUT GFX DIRECTORY] \
[VAGANTE GFX META JSON FILE] [VAGANTE PALETTE JSON FILE] [OPTIONS...]\"\n";
		cerr << "Usage notes: use Unix-style file paths, NOT windows style.\n";
		cerr << "eg. 'C:/git/path-to-stuff' instead of 'C:\\git\\path-to-stuff'.\n";
		cerr << "Multiple input directories can be supplied by separating them with commas.\n";
        return EXIT_FAILURE;
    }
    
    //Get the output directory and name
    string outputDir, outputPrefix;
    SplitFileName(argv[1], &outputDir, &outputPrefix, nullptr);

    //Get all the input files and directories
    vector<string> inputs;
    stringstream ss(argv[2]);
    while (ss.good())
    {
        string inputStr;
        getline(ss, inputStr, ',');
        inputs.push_back(inputStr);
    }
	//Get the gfx meta json file and parse it
	const string gfxMetaJsonFileName = argv[3];
	rapidjson::Document dGfxMeta;
	{
		std::ifstream inFileGfxMeta{ gfxMetaJsonFileName };
		if (!inFileGfxMeta.is_open())
		{
			cerr << "Failed to open gfx meta JSON file '" << gfxMetaJsonFileName << "'!\n";
			return EXIT_FAILURE;
		}
		std::stringstream ssFileGfxMeta;
		ssFileGfxMeta << inFileGfxMeta.rdbuf();
		dGfxMeta.Parse(ssFileGfxMeta.str().c_str());
		if (dGfxMeta.HasParseError())
		{
			cerr << "Failed to parse gfx meta JSON file '" << gfxMetaJsonFileName << "'!\n";
			cerr << "json error [" << dGfxMeta.GetErrorOffset() << "] =" <<
				dGfxMeta.GetParseError();
			return EXIT_FAILURE;
		}
	}
	//Get the palette json file and parse it
	const string palettesJsonFileName = argv[4];
	rapidjson::Document dPalettes;
	{
		std::ifstream inFilePalettes{ palettesJsonFileName };
		if (!inFilePalettes.is_open())
		{
			cerr << "Failed to open palettes JSON file '" << palettesJsonFileName << "'!\n";
			return EXIT_FAILURE;
		}
		std::stringstream ssFilePalettes;
		ssFilePalettes << inFilePalettes.rdbuf();
		dPalettes.Parse(ssFilePalettes.str().c_str());
		if (dPalettes.HasParseError())
		{
			cerr << "Failed to parse palettes JSON file '" << palettesJsonFileName << "'!\n";
			cerr << "json error [" << dPalettes.GetErrorOffset() << "] =" <<
				dPalettes.GetParseError();
			return EXIT_FAILURE;
		}
	}
    
    //Get the options
    optSize = 4096;
    optPadding = 2;
    optXml = false;
    optBinary = false;
    optJson = false;
    optPremultiply = false;
    optTrim = false;
    optVerbose = false;
    optForce = false;
    optUnique = false;
    for (int i = 5; i < argc; ++i)
    {
        string arg = argv[i];
        if (arg == "-d" || arg == "--default")
            optXml = optPremultiply = optTrim = optUnique = true;
        else if (arg == "-x" || arg == "--xml")
            optXml = true;
        else if (arg == "-b" || arg == "--binary")
            optBinary = true;
        else if (arg == "-j" || arg == "--json")
            optJson = true;
        else if (arg == "-p" || arg == "--premultiply")
            optPremultiply = true;
        else if (arg == "-t" || arg == "--trim")
            optTrim = true;
        else if (arg == "-v" || arg == "--verbose")
            optVerbose = true;
        else if (arg == "-f" || arg == "--force")
            optForce = true;
        else if (arg == "-u" || arg == "--unique")
            optUnique = true;
        else if (arg == "-r" || arg == "--rotate")
            optRotate = true;
        else if (arg.find("--size") == 0)
            optSize = GetPackSize(arg.substr(6));
        else if (arg.find("-s") == 0)
            optSize = GetPackSize(arg.substr(2));
        else if (arg.find("--pad") == 0)
            optPadding = GetPadding(arg.substr(5));
        else if (arg.find("-p") == 0)
            optPadding = GetPadding(arg.substr(2));
        else
        {
            cerr << "unexpected argument: " << arg << "\n";
            return EXIT_FAILURE;
        }
    }
	if (optVerbose)
	{
		cout << "SUPPLIED PARAMETERS:\n";
		cout << "outputDir=" << outputDir << "\n";
		cout << "outputPrefix=" << outputPrefix << "\n";
		cout << "input directories=\n";
		for (string const& input : inputs)
		{
			cout << "\t" << input << "\n";
		}
		cout << "gfxMetaJsonFileName=" << gfxMetaJsonFileName << "\n";
		cout << "palettesJsonFileName=" << palettesJsonFileName << "\n";
		cout << "END SUPPLIED PARAMETER OUTPUT.\n";
	}
    
    //Hash the arguments and input directories
	if (optVerbose)
	{
		cout << "Hashing arguments & input directories...";
	}
    size_t newHash = 0;
    for (int i = 1; i < argc; ++i)
        HashString(newHash, argv[i]);
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        if (inputs[i].rfind('.') == string::npos)
            HashFiles(newHash, inputs[i]);
        else
            HashFile(newHash, inputs[i]);
    }
	HashFile(newHash, gfxMetaJsonFileName);
	HashFile(newHash, palettesJsonFileName);
	if (optVerbose)
	{
		cout << "DONE!\n";
	}
    
    //Load the old hash
    size_t oldHash;
    if (LoadHash(oldHash, outputDir + outputPrefix + ".hash"))
    {
        if (!optForce && newHash == oldHash)
        {
            cout << "atlas is unchanged: " << outputPrefix << "\n";
            return EXIT_SUCCESS;
        }
    }
    
    /*-d  --default           use default settings (-x -p -t -u)
    -x  --xml               saves the atlas data as a .xml file
    -b  --binary            saves the atlas data as a .bin file
    -j  --json              saves the atlas data as a .json file
    -p  --premultiply       premultiplies the pixels of the bitmaps by their alpha channel
    -t  --trim              trims excess transparency off the bitmaps
    -v  --verbose           print to the debug console as the packer works
    -f  --force             ignore the hash, forcing the packer to repack
    -u  --unique            remove duplicate bitmaps from the atlas
    -r  --rotate            enabled rotating bitmaps 90 degrees clockwise when packing
    -s# --size#             max atlas size (# can be 4096, 2048, 1024, 512, or 256)
    -p# --pad#              padding between images (# can be from 0 to 16)*/
    
    if (optVerbose)
    {
        cout << "options...\n";
        cout << "\t--xml: " << (optXml ? "true" : "false") << "\n";
        cout << "\t--binary: " << (optBinary ? "true" : "false") << "\n";
        cout << "\t--json: " << (optJson ? "true" : "false") << "\n";
        cout << "\t--premultiply: " << (optPremultiply ? "true" : "false") << "\n";
        cout << "\t--trim: " << (optTrim ? "true" : "false") << "\n";
        cout << "\t--verbose: " << (optVerbose ? "true" : "false") << "\n";
        cout << "\t--force: " << (optForce ? "true" : "false") << "\n";
        cout << "\t--unique: " << (optUnique ? "true" : "false") << "\n";
        cout << "\t--rotate: " << (optRotate ? "true" : "false") << "\n";
        cout << "\t--size: " << optSize << "\n";
        cout << "\t--pad: " << optPadding << "\n";
    }
    
    //Remove old files
	const string processedGfxDir = outputDir + ".processed-gfx";
	const bool debugProcessedGfx = false;
	{
///		cout << "Recursively deleting old temp files from '" << processedGfxDir << "'...\n";
///		const uintmax_t numProcessedGfxDeleted = 
///			fs::remove_all(processedGfxDir);
///		cout << "Erased " << numProcessedGfxDeleted << " old processed gfx.\n";
		RemoveFile(outputDir + outputPrefix + ".hash");
		RemoveFile(outputDir + outputPrefix + ".bin");
		RemoveFile(outputDir + outputPrefix + ".xml");
		RemoveFile(outputDir + outputPrefix + ".json");
		for (size_t i = 0; i < 16; ++i)
			RemoveFile(outputDir + outputPrefix + to_string(i) + ".png");
	}
	// Load the palettes.json file contents into memory //
	if (optVerbose)
	{
		cout << "Deserializing palettes JSON file...\n";
	}
	{
		auto pGArray = dPalettes["palette-groups"].GetArray();
		for (rapidjson::SizeType pg = 0; pg < pGArray.Size(); pg++)
		{
			auto const& palGroup = pGArray[pg];
			PaletteGroup newPg;
			const string newPgName = palGroup["name"].GetString();
			if (optVerbose)
			{
				cout << "\tPaletteGroup name="<< newPgName<<"\n";
			}
			auto texNameArray = palGroup["texture-names"].GetArray();
			for (rapidjson::SizeType t = 0; t < texNameArray.Size(); t++)
			{
				char const*const texName = texNameArray[t].GetString();
				newPg.textureNames.emplace_back(texName);
			}
			auto pArray = palGroup["palettes"].GetArray();
			for (rapidjson::SizeType p = 0; p < pArray.Size(); p++)
			{
				auto const& jsonPalette = pArray[p];
				Palette newP;
				newP.name = jsonPalette["name"].GetString();
				if (optVerbose)
				{
					cout << "\t\tPalette name=" << newP.name << "\n";
				}
				auto colorArray = jsonPalette["colors"].GetArray();
				for (rapidjson::SizeType c = 0; c < colorArray.Size(); c++)
				{
					auto colorComponentArray = colorArray[c].GetArray();
					assert(colorComponentArray.Size() == 4);
					const uint32_t red   = colorComponentArray[0].GetInt();
					const uint32_t green = colorComponentArray[1].GetInt();
					const uint32_t blue  = colorComponentArray[2].GetInt();
					// Just ignore the alpha component because it is meaningless //
					///const uint32_t alpha = colorComponentArray[3].GetInt();
					const uint32_t colorData = 
						///(alpha << 24) |
						(blue  << 16) |
						(green << 8 ) |
						 red;
					newP.colors.push_back(colorData);
				}
				newPg.palettes.push_back(newP);
			}
			paletteGroups.push_back(newPg);
		}
	}
	// Process Vagante's gfx-meta.json file //
///	fs::create_directories(processedGfxDir);
///	cout << "processedGfxDir='" << processedGfxDir << "'\n";
	{
		vector<Bitmap*> flipbookBitmaps;
		vector<Bitmap*> frameBitmaps;
		struct FlipbookMeta
		{
			string fileNameAndGfxPathAndExt;
			int frameWidth;
			int frameHeight;
			int frameCount;
			bool generateMask;
			bool generateOutline;
		};
		vector<FlipbookMeta> flipbookMetaArray;
		const int numPlayerCostumes = dGfxMeta["num-player-costumes"].GetInt();
		auto playerClassDirArray = dGfxMeta["player-class-directories"].GetArray();
		auto petClassDirArray    = dGfxMeta["pet-class-directories"].GetArray();
		auto pcFbArray       = dGfxMeta["player-class-flipbooks"].GetArray();
		auto petFbArray      = dGfxMeta["pet-class-flipbooks"].GetArray();
		auto skeletonFbArray = dGfxMeta["skeleton-class-flipbooks"].GetArray();
		auto fbArray         = dGfxMeta["flipbooks"].GetArray();
		auto gatherFlipbookMeta = [&flipbookMetaArray](string const& fileNamePrefix,
			rapidjson::GenericArray<false, rapidjson::Value::ValueType> const& jsonArray)->void
		{
			for (rapidjson::SizeType fb = 0; fb < jsonArray.Size(); fb++)
			{
				auto const& flipbookMeta = jsonArray[fb];
				FlipbookMeta newMeta;
				newMeta.fileNameAndGfxPathAndExt = 
					fileNamePrefix + flipbookMeta["filename"].GetString();
				newMeta.frameWidth               = flipbookMeta["frame-width"].GetInt();
				newMeta.frameHeight              = flipbookMeta["frame-height"].GetInt();
				newMeta.frameCount               = flipbookMeta["frame-count"].GetInt();
				newMeta.generateMask             = flipbookMeta["generate-mask"].GetBool();
				newMeta.generateOutline          = flipbookMeta["generate-outline"].GetBool();
				if (optVerbose)
				{
					cout << "new flipbook fileName=" << newMeta.fileNameAndGfxPathAndExt << "\n";
					///cout << "new flipbook name=" << flipbookBitmaps.back()->name << "\n";
				}
				flipbookMetaArray.push_back(newMeta);
			}
		};
		for (int playerCostumeIndex = 1; playerCostumeIndex <= numPlayerCostumes; playerCostumeIndex++)
		{
			for (rapidjson::SizeType d = 0; d < playerClassDirArray.Size(); d++)
			{
				char const*const classDir = playerClassDirArray[d].GetString();
				stringstream ssDir;
				ssDir << classDir << playerCostumeIndex << "/";
				gatherFlipbookMeta(ssDir.str(), pcFbArray);
				if (ssDir.str().find("skeleton") != string::npos)
				{
					gatherFlipbookMeta(ssDir.str(), skeletonFbArray);
				}
			}
			for (rapidjson::SizeType d = 0; d < petClassDirArray.Size(); d++)
			{
				char const*const classDir = petClassDirArray[d].GetString();
				stringstream ssDir;
				ssDir << classDir << playerCostumeIndex << "/";
				gatherFlipbookMeta(ssDir.str(), petFbArray);
			}
		}
		gatherFlipbookMeta("", fbArray);
		for(FlipbookMeta const& fbMeta : flipbookMetaArray)
		{
			char const*const fbFileNameAndGfxPathAndExt = 
				fbMeta.fileNameAndGfxPathAndExt.c_str();
			int frameW                 = fbMeta.frameWidth;
			int frameH                 = fbMeta.frameHeight;
			int frameCount             = fbMeta.frameCount;
			const bool generateMask    = fbMeta.generateMask;
			const bool generateOutline = fbMeta.generateOutline;
			string fbFileDir, fbFileName;
			SplitFileName(fbFileNameAndGfxPathAndExt, &fbFileDir, &fbFileName, nullptr);
///			if (optVerbose)
///			{
///				cout << "processing flipbook '" << fbFileName << "'...\n";
///			}
			///loadBitmap(fbFileDir, inputs[0] + "/" + fbFileNameAndGfxPathAndExt, flipbookBitmaps);
			const string absoluteFileName = inputs[0] + "/" + fbFileNameAndGfxPathAndExt;
			// specifically do NOT trim the flipbook sprite sheet when we load it in here!
			//	we will do the trim step on each individual frame instead to save maximum space.
			flipbookBitmaps.push_back(new Bitmap(
				absoluteFileName,
				fbFileDir + GetFileName(absoluteFileName),
				optPremultiply, false));
			// if the database has w == h == 0, that means the entire flipbook should just be treated
			//	as a single frame.
			if (frameW == 0 || frameH == 0)
			{
				if (frameW != 0 || frameH != 0)
				{
					cerr << "ERROR: frame-width & frame-height must BOTH be equal to zero if either "
						<< "one is zero to signify a degenerate case single-frame flipbook!\n";
					cerr << "Offending flipbook=" << fbFileNameAndGfxPathAndExt << "\n";
					exit(EXIT_FAILURE);
				}
				frameW = flipbookBitmaps.back()->width;
				frameH = flipbookBitmaps.back()->height;
				frameCount = 0;
			}
			const int numFrames = (frameCount > 0 ? frameCount :  
				((flipbookBitmaps.back()->width  / frameW) * 
				 (flipbookBitmaps.back()->height / frameH)));
///			cout << fbFileDir << "\n";
///			cout << (processedFlipbookDir + "/" + fbFileDir) << "\n";
			// Create a directory to store all the processed flipbook sprites in a temp folder //
			//	(Actually, maybe don't do this and just store the sprites to be processed in memory?)
			//	(I guess this would only be useful for debugging, we'll see...)
			if (debugProcessedGfx)
			{
				fs::create_directories(processedGfxDir + "/flipbooks/" + fbFileDir + fbFileName);
				if (generateMask)
				{
					fs::create_directories(processedGfxDir + "/flipbooks/" + fbFileDir + fbFileName + "/mask");
				}
				if (generateOutline)
				{
					fs::create_directories(processedGfxDir + "/flipbooks/" + fbFileDir + fbFileName + "/outline");
				}
			}
			// iterate over paletteGroups, iterate over each PaletteGroup's textureNames,
			//	if it contains fbFileNameAndGfxPathAndExt in this PaletteGroup, that means we need
			//	to also generate palette swaps for each frame in the loop below!
			// @assumtion
			//	for any given texture file, it is only located in ONE palette group!
			PaletteGroup const* flipbookPaletteGroup = nullptr;
			for (auto const& pg : paletteGroups)
			{
				for (string const& texName : pg.textureNames)
				{
					if (fbFileNameAndGfxPathAndExt == texName)
					{
						flipbookPaletteGroup = &pg;
						break;
					}
				}
				if (flipbookPaletteGroup)
				{
					break;
				}
			}
			const int numColumns = flipbookBitmaps.back()->width / frameW;
			for (int f = 0; f < numFrames; f++)
			{
				const int frameOffsetX = (f % numColumns) * frameW;
				const int frameOffsetY = (f / numColumns) * frameH;
				stringstream ssFrameName;
				ssFrameName << fbFileDir << fbFileName << "/" << f;
				if (optVerbose)
				{
					cout << "\t" << ssFrameName.str()<<"\n";
				}
				frameBitmaps.push_back(new Bitmap(flipbookBitmaps.back(),
					frameOffsetX, frameOffsetY, frameW, frameH,
					ssFrameName.str(),
					// do not premultiply on the individual frames, since we already 
					//	did that w/ the entire flipbook texture
					false, optTrim));
				bitmaps.push_back(new Bitmap(*frameBitmaps.back()));
				if (debugProcessedGfx)
				{
					stringstream ss;
					ss << (processedGfxDir + "/flipbooks/" + fbFileDir + fbFileName + "/");
					ss << f << ".png";
					frameBitmaps.back()->SaveAs(ss.str());
				}
				if (generateMask)
				{
					bitmaps.push_back(new Bitmap(*frameBitmaps.back()));
					stringstream ssFrameName;
					ssFrameName << fbFileDir << fbFileName << "/mask/" << f;
					bitmaps.back()->maskPixels(ssFrameName.str());
					if (debugProcessedGfx)
					{
						stringstream ss;
						ss << (processedGfxDir + "/flipbooks/" + fbFileDir + fbFileName + "/mask/");
						ss << f << ".png";
						bitmaps.back()->SaveAs(ss.str());
					}
				}
				if (generateOutline)
				{
					bitmaps.push_back(new Bitmap(*frameBitmaps.back()));
					stringstream ssFrameName;
					ssFrameName << fbFileDir << fbFileName << "/outline/" << f;
					bitmaps.back()->outlinePixels(ssFrameName.str());
					if (debugProcessedGfx)
					{
						stringstream ss;
						ss << (processedGfxDir + "/flipbooks/" + fbFileDir + fbFileName + "/outline/");
						ss << f << ".png";
						bitmaps.back()->SaveAs(ss.str());
					}
				}
				if (flipbookPaletteGroup)
				{
					// @assumption
					//	first palette in a palette group is always the default palette
					Palette const& defaultPalette = flipbookPaletteGroup->palettes[0];
					for (size_t p = 1; p < flipbookPaletteGroup->palettes.size(); p++)
					{
						Palette const& palette = flipbookPaletteGroup->palettes[p];
						bitmaps.push_back(new Bitmap(*frameBitmaps.back()));
						stringstream ssFrameName;
						ssFrameName << fbFileDir << fbFileName << "/"<<
							palette.name <<"/"<< f;
						bitmaps.back()->swapPalette(ssFrameName.str(),
							defaultPalette.colors, palette.colors);
						if (debugProcessedGfx)
						{
							fs::create_directories(processedGfxDir + "/flipbooks/" + 
								fbFileDir + fbFileName + "/"+ palette.name);
							stringstream ss;
							ss << (processedGfxDir + "/flipbooks/" + fbFileDir + fbFileName + "/");
							ss << palette.name << "/" << f << ".png";
							bitmaps.back()->SaveAs(ss.str());
						}
					}
				}
			}
		}
	}

///    //Load the bitmaps from all the input files and directories
///    if (optVerbose)
///        cout << "loading images..." << endl;
///    for (size_t i = 0; i < inputs.size(); ++i)
///    {
///        if (inputs[i].rfind('.') != string::npos)
///            loadBitmap("", inputs[i], bitmaps);
///        else
///            LoadBitmaps(inputs[i], "", bitmaps);
///    }
    
    //Sort the bitmaps by area
    sort(bitmaps.begin(), bitmaps.end(), [](const Bitmap* a, const Bitmap* b) {
        return (a->width * a->height) < (b->width * b->height);
    });
    
    //Pack the bitmaps
    while (!bitmaps.empty())
    {
        if (optVerbose)
            cout << "packing " << bitmaps.size() << " images..." << endl;
        auto packer = new Packer(optSize, optSize, optPadding);
        packer->Pack(bitmaps, optVerbose, optUnique, optRotate);
        packers.push_back(packer);
        if (optVerbose)
            cout << "finished packing: " << outputPrefix << to_string(packers.size() - 1) << " (" << packer->width << " x " << packer->height << ')' << endl;
    
        if (packer->bitmaps.empty())
        {
            cerr << "packing failed, could not fit bitmap: " << (bitmaps.back())->name << endl;
            return EXIT_FAILURE;
        }
    }
    
    //Save the atlas image
    for (size_t i = 0; i < packers.size(); ++i)
    {
        if (optVerbose)
            cout << "writing png: " << outputDir << outputPrefix << to_string(i) << ".png" << endl;
        packers[i]->SavePng(outputDir + outputPrefix + to_string(i) + ".png");
    }
    
    //Save the atlas binary
    if (optBinary)
    {
        if (optVerbose)
            cout << "writing bin: " << outputDir << outputPrefix << ".bin" << endl;
        
        ofstream bin(outputDir + outputPrefix + ".bin", ios::binary);
        WriteShort(bin, (int16_t)packers.size());
        for (size_t i = 0; i < packers.size(); ++i)
            packers[i]->SaveBin(outputPrefix + to_string(i), bin, optTrim, optRotate);
        bin.close();
    }
    
    //Save the atlas xml
    if (optXml)
    {
        if (optVerbose)
            cout << "writing xml: " << outputDir << outputPrefix << ".xml" << endl;
        
        ofstream xml(outputDir + outputPrefix + ".xml");
        xml << "<atlas>" << endl;
        for (size_t i = 0; i < packers.size(); ++i)
            packers[i]->SaveXml(outputPrefix + to_string(i), xml, optTrim, optRotate);
        xml << "</atlas>";
    }
    
    //Save the atlas json
    if (optJson)
    {
        if (optVerbose)
            cout << "writing json: " << outputDir << outputPrefix << ".json" << endl;
        
        ofstream json(outputDir + outputPrefix + ".json");
        json << '{' << endl;
        json << "\t\"textures\":[" << endl;
        for (size_t i = 0; i < packers.size(); ++i)
        {
            json << "\t\t{" << endl;
            packers[i]->SaveJson(outputPrefix + to_string(i), json, optTrim, optRotate);
            json << "\t\t}";
            if (i + 1 < packers.size())
                json << ',';
            json << endl;
        }
        json << "\t]" << endl;
        json << '}';
    }
    
    //Save the new hash
    SaveHash(newHash, outputDir + outputPrefix + ".hash");
    
    return EXIT_SUCCESS;
}
