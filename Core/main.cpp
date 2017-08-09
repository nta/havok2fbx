// Copyright 2015 Highflex
// Please check README.MD for full credits

#include "stdafx.h"
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>    // copy
#include <iterator>     // back_inserter
#include <regex>        // regex, sregex_token_iterator

// HAVOK stuff now
#include <Common/Base/hkBase.h>
#include <Common/Base/Container/String/hkStringBuf.h>
#include <Common/Base/Memory/System/Util/hkMemoryInitUtil.h>
#include <Common/Base/Memory/Allocator/Malloc/hkMallocAllocator.h>
#include <Common/Base/System/Io/IStream/hkIStream.h>

#include <Common/Base/Reflection/Registry/hkDefaultClassNameRegistry.h>
#include <Common/Serialize/Util/hkStaticClassNameRegistry.h>

#include <cstdio>

// Compatibility
#include <Common/Compat/hkCompat.h>

// Scene
#include <Common/SceneData/Scene/hkxScene.h>

#include <Common/Base/Fwd/hkcstdio.h>

// Geometry
#include <Common/Base/Types/Geometry/hkGeometry.h>

// Serialize
#include <Common/Serialize/Util/hkRootLevelContainer.h>
#include <Common/Serialize/Util/hkLoader.h>
#include <Common/Serialize/Util/hkSerializeUtil.h>
#include <Common/Serialize/Version/hkVersionPatchManager.h>
#include <Common/Serialize/Data/hkDataObject.h>

// Animation
#include <Animation/Animation/Rig/hkaSkeleton.h>
#include <Animation/Animation/hkaAnimationContainer.h>
#include <Animation/Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Animation/Playback/Control/Default/hkaDefaultAnimationControl.h>
#include <Animation/Animation/Playback/hkaAnimatedSkeleton.h>
#include <Animation/Animation/Animation/SplineCompressed/hkaSplineCompressedAnimation.h>
#include <Animation/Animation/Rig/hkaPose.h>
#include <Animation/Animation/Rig/hkaSkeletonUtils.h>

// Reflection
#include <Common/Base/Reflection/hkClass.h>
#include <Common/Base/Reflection/hkClassMember.h>
#include <Common/Base/Reflection/hkInternalClassMember.h>
#include <Common/Base/Reflection/hkClassMemberAccessor.h>

// Utils
#include "hkAssetManagementUtil.h"
#include "MathHelper.h"
#include "EulerAngles.h"

// FBX
//#include <fbxsdk.h>
//#include "FBXCommon.h" // samples common path, todo better way

// FBX Function prototypes.
/*bool CreateScene(FbxManager* pSdkManager, FbxScene* pScene); // create FBX scene
FbxNode* CreateSkeleton(FbxScene* pScene, const char* pName); // create the actual skeleton
void AnimateSkeleton(FbxScene* pScene, FbxNode* pSkeletonRoot); // add animation to it*/

void PlatformInit();
void PlatformFileSystemInit();

static void HK_CALL errorReport(const char* msg, void* userContext)
{
	using namespace std;
	printf("%s", msg);
}

bool file_exists(const char *fileName)
{
    std::ifstream infile(fileName);
    return infile.good();
}

// http://stackoverflow.com/questions/6417817/easy-way-to-remove-extension-from-a-filename
std::string remove_extension(const std::string& filename) 
{
    size_t lastdot = filename.find_last_of(".");
    if (lastdot == std::string::npos) return filename;
    return filename.substr(0, lastdot); 
}

static void show_usage(std::string name)
{
	// TODO: better versioning
    std::cerr << "havok2fbx Version 0.1a public by Highflex 2015\n\n" 
				<< "Options:\n"
				<< "\t-h,--help\n\tShow this help message\n\n"
				<< "\t-hk_skeleton,--havokskeleton PATH\n\tSpecify the Havok skeleton file\n\n"
				<< "\t-hk_anim,--havokanim PATH\n\tSpecify the Havok animation file\n\n"
				<< "\t-fbx,--fbxfile PATH\n\tSpecify the FBX output file to save\n\n"
				<< std::endl;
}

// global so we can access this later
class hkLoader* m_loader;
class hkaSkeleton* m_skeleton;
class hkaAnimation* m_animation;
class hkaAnimationBinding* m_binding;

bool bAnimationGiven = false;

#define HK_GET_DEMOS_ASSET_FILENAME(fname) fname

// Define a useful macro for this demo - it allow us to detect a failure, print a message, and return early
#define RETURN_FAIL_IF(COND, MSG) \
	HK_MULTILINE_MACRO_BEGIN \
		if(COND) { HK_ERROR(0x53a6a026, MSG); return 1; } \
	HK_MULTILINE_MACRO_END

#if defined(HK_PLATFORM_WINRT) || defined(HK_PLATFORM_DURANGO)
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^ args)
#elif defined( HK_PLATFORM_TIZEN )
int hkMain(int argc, const char** argv)
#else
int HK_CALL main(int argc, const char** argv)
#endif
{
	// user needs to specify only the input file
	// if no output argument was given just assume same path as input and write file there
	if (argc < 2) 
	{
        show_usage(argv[0]);
        return 1;
    }

    hkStringBuf havokskeleton;
	hkStringBuf havokanim;
	const char* fbxfile = NULL;
	std::string havok_path_backup;

	bool bSkeletonIsValid = false;
	bool xmlMode = false;
	
    for (int i = 1; i < argc; ++i) 
	{
        std::string arg = argv[i];

        if ((arg == "-h") || (arg == "--help")) 
		{
            show_usage(argv[0]);
            return 0;
        } 
		else 
		{
			// skeleton is required
			if ((arg == "-hk_skeleton") || (arg == "--havokskeleton")) 
			{
				if (i + 1 < argc)
				{
					// check if file is valid before going on
					if(file_exists(argv[i+1]))
					{
						bSkeletonIsValid = true;
						havokskeleton = argv[i+1];
						havok_path_backup = argv[i+1];
						std::cout << "HAVOK SKELETON FILEPATH IS: " << havokskeleton << "\n";
					}
					else
					{
						std::cerr << "ERROR: specified havok skeleton file doesn't exist!" << std::endl;
						return 1;
					}
				} 
				else 
				{
					std::cerr << "--havokskeleton option requires path argument." << std::endl;
					return 1;
				} 
			}

			if ((arg == "-hk_anim") || (arg == "--havokanim") && bSkeletonIsValid) 
			{
				if (i + 1 < argc)
				{
					// check if file is valid before going on
					if(file_exists(argv[i+1]))
					{
						havokanim = argv[i+1];
						std::cout << "HAVOK ANIMATION FILEPATH IS: " << havokanim << "\n";
						bAnimationGiven = true;
					}
					else
					{
						std::cerr << "ERROR: specified havok animation file doesn't exist!" << std::endl;
						return 1;
					}
				}
			}

			if (arg == "-xml")
			{
				xmlMode = true;
			}

			if ((arg == "-out") || (arg == "--outfile")) 
			{
				if (i + 1 < argc)
				{
					fbxfile = argv[i+1];
					std::cout << "OUT FILEPATH IS: " << fbxfile << "\n";
				} 
				else 
				{
					std::cerr << "--outfile option requires path argument." << std::endl;
					return 1;
				} 
			}
        } 
    }

	// Perfrom platform specific initialization for this demo - you should already have something similar in your own code.
	PlatformInit();

	// Need to have memory allocated for the solver. Allocate 1mb for it.
	hkMemoryRouter* memoryRouter = hkMemoryInitUtil::initDefault( hkMallocAllocator::m_defaultMallocAllocator, hkMemorySystem::FrameInfo(1024 * 1024) );
	hkBaseSystem::init( memoryRouter, errorReport );

	// Set up platform-specific file system info.
	//PlatformFileSystemInit();

	{
		// load skeleton first!
		m_loader = new hkLoader();

		{
			hkStringBuf assetFile(havokskeleton); hkAssetManagementUtil::getFilePath(assetFile);
			hkRootLevelContainer* container = m_loader->load( HK_GET_DEMOS_ASSET_FILENAME(assetFile.cString()) );

			if (xmlMode)
			{
				hkSerializeUtil::save(container, fbxfile, hkSerializeUtil::SAVE_TEXT_FORMAT);
				return 0;
			}

			//hkSerializeUtil::save(container, "B:\\tmp\\thing.xml", hkSerializeUtil::SAVE_TEXT_FORMAT);

			HK_ASSERT2(0x27343437, container != HK_NULL , "Could not load asset");
			hkaAnimationContainer* ac = reinterpret_cast<hkaAnimationContainer*>( container->findObjectByType( hkaAnimationContainerClass.getName() ));

			HK_ASSERT2(0x27343435, ac && (ac->m_skeletons.getSize() > 0), "No skeleton loaded");
			m_skeleton = ac->m_skeletons[0];

			FILE* f = fopen(fbxfile, "w");

			if (!f)
			{
				return 1;
			}

			fprintf(f, "nodes\n");

			for (int i = 0; i < m_skeleton->m_bones.getSize(); i++)
			{
				fprintf(f, "%d \"%s\" %d\n", i, m_skeleton->m_bones[i].m_name.cString(), m_skeleton->m_parentIndices[i]);
			}

			fprintf(f, "end\n\nskeleton\ntime 0\n");

			for (int i = 0; i < m_skeleton->m_referencePose.getSize(); i++)
			{
				hkQsTransform trans = m_skeleton->m_referencePose[i];
				/*hkQsTransform parentTrans;
				parentTrans.setIdentity();

				if (m_skeleton->m_parentIndices[i] >= 0)
				{
					hkQsTransform invTrans;
					invTrans.setInverse(m_skeleton->m_referencePose[m_skeleton->m_parentIndices[i]]);

					trans.setMulEq(invTrans);
				}*/

				hkQuaternion a;
				
				float rx, ry, rz;

				{
					float x = trans.getRotation().getImag().getSimdAt(0);
					float y = trans.getRotation().getImag().getSimdAt(1);
					float z = trans.getRotation().getImag().getSimdAt(2);
					float w = trans.getRotation().getReal();

					rx = atan2(2.0f * (y * z + w * x), w * w - x * x - y * y + z * z);
					ry = asin(-2.0f * (x * z - w * y));
					rz = atan2(2.0f * (x * y + w * z), w * w + x * x - y * y - z * z);
				}

				float x = trans.getTranslation().getSimdAt(0);
				float y = trans.getTranslation().getSimdAt(1);
				float z = trans.getTranslation().getSimdAt(2);

				fprintf(f, "%d    %f %f %f    %f %f %f\n", i, x, y, z, rx, ry, rz);
			}

			fprintf(f, "end\n");
		}

		// todo delete stuff after usage
	}

	// after gathering havok data, write the stuff now into a FBX
	// INIT FBX
#if 0
	FbxManager* lSdkManager = NULL;
    FbxScene* lScene = NULL;
    bool lResult;

    // Prepare the FBX SDK.
    InitializeSdkObjects(lSdkManager, lScene);

	// Create the scene.
    lResult = CreateScene(lSdkManager, lScene);

    if(lResult == false)
    {
        FBXSDK_printf("\n\nAn error occurred while creating the scene...\n");
        DestroySdkObjects(lSdkManager, lResult);
        return 0;
    }

	// Save the scene to FBX.
	// check if the user has given a FBX filename/path if not use the same location as the havok input
	// The example can take an output file name as an argument.
	const char* lSampleFileName = NULL;
	std::string fbx_extension = ".fbx";
	std::string combined_path;

	if(fbxfile != NULL)
	{
		lSampleFileName = fbxfile;
	}
	else
	{
		// get havok skel path and trim the extension from it		
		combined_path = remove_extension(havok_path_backup) + fbx_extension;
		lSampleFileName = combined_path.c_str();

		std::cout << "\n" << "Saving FBX to: " << lSampleFileName << "\n";
	}

	lResult = SaveScene(lSdkManager, lScene, lSampleFileName);

    if(lResult == false)
    {
        FBXSDK_printf("\n\nAn error occurred while saving the scene...\n");
        DestroySdkObjects(lSdkManager, lResult);
        return 0;
    }

    // Destroy all objects created by the FBX SDK.
    DestroySdkObjects(lSdkManager, lResult);
#endif

	// destroy objects not required
	hkBaseSystem::quit();
	hkMemoryInitUtil::quit();

	return 0;
}

// [id=keycode]
#include <Common/Base/keycode.cxx>

// [id=productfeatures]
// We're not using anything product specific yet. We undef these so we don't get the usual
// product initialization for the products.
#undef HK_FEATURE_PRODUCT_AI
//#undef HK_FEATURE_PRODUCT_ANIMATION
#undef HK_FEATURE_PRODUCT_CLOTH
#undef HK_FEATURE_PRODUCT_DESTRUCTION_2012
#undef HK_FEATURE_PRODUCT_DESTRUCTION
#undef HK_FEATURE_PRODUCT_BEHAVIOR
#undef HK_FEATURE_PRODUCT_PHYSICS_2012
#undef HK_FEATURE_PRODUCT_SIMULATION
#undef HK_FEATURE_PRODUCT_PHYSICS

// We are using serialization, so we need ReflectedClasses.
// The objects are being saved and then loaded immediately so we know the version of the saved data is the same 
// as the version the application is linked with. Because of this we don't need RegisterVersionPatches or SerializeDeprecatedPre700.
// If the demo was reading content saved from a previous version of the Havok content tools (common in real world Applications) 
// RegisterVersionPatches and perhaps SerializeDeprecatedPre700 are needed.

//#define HK_EXCLUDE_FEATURE_SerializeDeprecatedPre700

// We can also restrict the compatibility to files created with the current version only using HK_SERIALIZE_MIN_COMPATIBLE_VERSION.
// If we wanted to have compatibility with at most version 650b1 we could have used something like:
// #define HK_SERIALIZE_MIN_COMPATIBLE_VERSION 650b1.
#define HK_SERIALIZE_MIN_COMPATIBLE_VERSION 2

//#define HK_EXCLUDE_FEATURE_RegisterVersionPatches
//#define HK_EXCLUDE_FEATURE_RegisterReflectedClasses
//#define HK_EXCLUDE_FEATURE_MemoryTracker

// We also need to exclude the other common libraries referenced in Source\Common\Serialize\Classlist\hkCommonClasses.h
// You may be linking these libraries in your application, in which case you will not need these #defines.
//#define HK_EXCLUDE_LIBRARY_hkcdCollide
//#define HK_EXCLUDE_LIBRARY_hkcdInternal
//#define HK_EXCLUDE_LIBRARY_hkSceneData
//#define HK_EXCLUDE_LIBRARY_hkGeometryUtilities

#include <Common/Base/Config/hkProductFeatures.cxx>

// Platform specific initialization
#include <Common/Base/System/Init/PlatformInit.cxx>