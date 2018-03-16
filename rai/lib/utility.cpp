#include <boost/algorithm/string.hpp>
#include <cryptopp/hex.h>
#include <cryptopp/sha.h>
#include <fstream>
#include <iostream>
#include <rai/lib/utility.hpp>

std::vector<char> rai::read_file (std::string path, bool & error)
{
	error = false;
	std::ifstream file (path.c_str (), std::ios::binary | std::ios::ate);
	std::streamsize size = file.tellg ();
	file.seekg (0, std::ios::beg);

	std::vector<char> buffer (size);
	if (!file.read (buffer.data (), size))
	{
		error = true;
	}

	return buffer;
}

std::string rai::sha256 (std::vector<char> buffer)
{
	return rai::sha256 (reinterpret_cast<const unsigned char *> (buffer.data ()), buffer.size ());
}

std::string rai::sha256 (const unsigned char * bytes, size_t len)
{
	CryptoPP::SHA256 hash;
	CryptoPP::byte digest[CryptoPP::SHA256::DIGESTSIZE];
	hash.CalculateDigest (digest, bytes, len);
	CryptoPP::HexEncoder encoder;
	std::string hex;
	encoder.Attach (new CryptoPP::StringSink (hex));
	encoder.Put (digest, CryptoPP::SHA256::DIGESTSIZE);
	encoder.MessageEnd ();
	boost::algorithm::to_lower (hex);
	return hex;
}
