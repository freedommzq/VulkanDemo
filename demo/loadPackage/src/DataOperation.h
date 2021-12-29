#pragma once

#include "cereal/types/unordered_map.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/map.hpp"

#include <vector>
#include <map>
#include <fstream>

enum ModelType {
	CAR,STATIC
};


struct SVec3
{
	SVec3() {
	}
	SVec3(float a, float b, float c) {
		x = a; y = b; z = c;
	}
	float x, y, z;
	template <class Archive>
	void serialize(Archive& ar, std::uint32_t const version)
	{
		ar(x, y, z);
	}
};

struct SVec4
{
	SVec4() {
	}
	SVec4(float a, float b, float c, float d) {
		x = a; y = b; z = c; w = d;
	}
	float x, y, z, w;
	template <class Archive>
	void serialize(Archive& ar, std::uint32_t const version)
	{
		ar(x, y, z, w);
	}
};

struct SMaterial
{
	std::map<int, unsigned int> mode;
	template <class Archive>
	void serialize(Archive& ar, std::uint32_t const version)
	{
		ar(mode);
	}
};

struct SGeodata
{
public:
	SGeodata() {
	}
	std::string geoName;
	std::vector<SVec3> vt;
	std::vector<SVec3> nm;
	std::vector<unsigned int> indices;
	std::vector<SVec3> tx[8];
	std::vector<SVec4> color;
	SMaterial ss;

	template <class Archive>
	void serialize(Archive& ar, std::uint32_t const version)
	{
		ar(geoName,vt, nm, indices, tx, color,ss);  //?
	}
};

struct SImage
{
	SImage() {
	}
	std::string imageName;
	int s, t, r;
	unsigned int pixelFormat, type;
	int packing;
	int internalTextureformat;
	std::vector<unsigned char> imageData;

	template <class Archive>
	void serialize(Archive& ar, std::uint32_t const version)
	{
		ar(imageName, s,t,r, pixelFormat, type, packing, internalTextureformat, imageData);
	}
};



struct SDataOperation
{
public:
	SVec3 LW_Center, RW_Center, BW_Center;
	
	int modelTpye;
	std::vector<SGeodata> geo;

	std::vector<SImage> m_images;

	std::vector<char> skipNodeBuf;

	std::string vertShader,fragShader;


	template <class Archive>
	void serialize(Archive& ar, std::uint32_t const version)
	{
		switch (version)
		{
		case CAR:	ar(geo, LW_Center, RW_Center, BW_Center, m_images, vertShader, fragShader, modelTpye, skipNodeBuf);
			break;
		case STATIC:	ar(geo, LW_Center, m_images, vertShader, fragShader, modelTpye);
			break; 
		default:
			break;
		}
		
	}
};


CEREAL_CLASS_VERSION(SDataOperation, 0);
CEREAL_CLASS_VERSION(SImage, 0);
CEREAL_CLASS_VERSION(SGeodata, 0);
CEREAL_CLASS_VERSION(SMaterial, 0);
CEREAL_CLASS_VERSION(SVec4, 0);
CEREAL_CLASS_VERSION(SVec3, 0);



