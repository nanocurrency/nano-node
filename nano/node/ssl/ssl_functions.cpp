#include <nano/node/ssl/ssl_functions.hpp>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <chrono>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include <crypto/ed25519-donna/ed25519.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace nano::ssl
{
using namespace std::chrono_literals;

namespace detail
{
	constexpr auto ONE_YEAR_DURATION = 24h * 365;
}

constexpr std::size_t ED25519_PRIVATE_KEY_SIZE = 32;
constexpr std::size_t ED25519_PUBLIC_KEY_SIZE = 32;
constexpr std::size_t ED25519_SIGNATURE_SIZE = 64;
constexpr std::size_t X509_CERTIFICATES_VERSION = 2;
constexpr std::string_view CA_CERTIFICATE_NAME = "Nano Node Root CA";
constexpr std::uint64_t CA_CERTIFICATE_SERIAL_NUMBER = 1;
constexpr std::string_view ADDITIONAL_SIGNATURES_EXTENSION_OBJECT_ID = "1.3.6.1.4.1.54392.5.1373";
constexpr std::string_view ADDITIONAL_SIGNATURES_EXTENSION_OBJECT_SHORT_NAME = "Additional Signatures";
constexpr std::string_view ADDITIONAL_SIGNATURES_EXTENSION_OBJECT_LONG_NAME = "X509 extension containing a sequence of SubjectPublicKeyInfo + X509_SIG_INFO objects";
constexpr std::size_t ADDITIONAL_SIGNATURES_DUMMY_COUNT = 5;
constexpr std::size_t CA_CERTIFICATE_VALIDITY_SECONDS = std::chrono::duration_cast<std::chrono::seconds> (30 * detail::ONE_YEAR_DURATION).count ();
constexpr std::string_view CA_CERTIFICATE_PEM_FILE = "CA.pem";

constexpr std::string_view INTERMEDIATE_CERTIFICATE_NAME = "Nano Node Intermediate CA";
constexpr std::uint64_t INTERMEDIATE_CERTIFICATE_SERIAL_NUMBER = 2;
constexpr std::size_t INTERMEDIATE_CERTIFICATE_VALIDITY_SECONDS = std::chrono::duration_cast<std::chrono::seconds> (5 * detail::ONE_YEAR_DURATION).count ();
constexpr std::string_view INTERMEDIATE_PRIVATE_KEY_PEM_FILE = "intermediate.prv.pem";
constexpr std::string_view INTERMEDIATE_PUBLIC_KEY_PEM_FILE = "intermediate.pub.pem";
constexpr std::string_view INTERMEDIATE_CERTIFICATE_PEM_FILE = "intermediate.pem";
constexpr std::string_view LEAF_CERTIFICATE_NAME = "Nano Node Connection Certificate";
constexpr std::uint64_t LEAF_CERTIFICATE_SERIAL_NUMBER = 3;
constexpr std::size_t LEAF_CERTIFICATE_VALIDITY_SECONDS = std::chrono::duration_cast<std::chrono::seconds> (detail::ONE_YEAR_DURATION).count ();
constexpr std::string_view LEAF_PUBLIC_KEY_PEM_FILE = "leaf.pub.pem";
constexpr std::string_view LEAF_CERTIFICATE_PEM_FILE = "leaf.pem";

std::string binaryToHex (const BufferView & input)
{
	std::stringstream stream{};
	stream << std::hex << std::setfill ('0');

	for (const auto byte : input)
	{
		stream << std::setw (2) << static_cast<std::uint32_t> (byte);
	}

	return stream.str ();
}

Buffer hexToBinary (const std::string_view & input)
{
	if (input.size () % 2)
	{
		throw std::runtime_error{ "hexToBinary: unexpected odd input size" };
	}

	if (input.empty ())
	{
		return Buffer{};
	}

	Buffer result{};
	result.reserve (input.size () / 2);

	for (auto itr = input.cbegin (); itr < std::prev (input.cend ()); std::advance (itr, 2))
	{
		const auto lhs = *itr;
		const auto rhs = *std::next (itr);

		result.push_back (std::stoi (std::string{ lhs } + rhs, nullptr, 16));
	}

	return result;
}

Buffer getCaPrivateKey (key_group const & key_group)
{
	return hexToBinary (key_group.key_private);
}

Buffer getCaPublicKey (key_group const & key_group)
{
	return hexToBinary (key_group.key_public);
}

std::string readFromBio (const BioPtrView & bio)
{
	BufferPtrView buffer{};

	const auto bufferLength = BIO_get_mem_data (*bio, buffer.getAddress ());
	if (bufferLength < 0)
	{
		throw std::runtime_error{ "readFromBio: BIO_get_mem_data: error" };
	}

	if (!bufferLength)
	{
		return std::string{};
	}

	return std::string{ reinterpret_cast<const char *> (*buffer), static_cast<std::size_t> (bufferLength) };
}

int getSslExDataIndex ()
{
	static const int result = SSL_get_ex_new_index (0, nullptr, nullptr, nullptr, nullptr);
	if (-1 == result)
	{
		throw std::runtime_error{ "getSslExDataIndex: SSL_get_ex_new_index " + getLastOpenSslError () };
	}

	return result;
}

int getAdditionalSignaturesExtensionObjectNid ()
{
	// TODO: work with OID textual representation instead, the custom NID is not reliable

	static const int result = OBJ_create (
	ADDITIONAL_SIGNATURES_EXTENSION_OBJECT_ID.data (),
	ADDITIONAL_SIGNATURES_EXTENSION_OBJECT_SHORT_NAME.data (),
	ADDITIONAL_SIGNATURES_EXTENSION_OBJECT_LONG_NAME.data ());

	if (result < 0 || NID_undef == result)
	{
		throw std::runtime_error{ "getAdditionalSignaturesExtensionObjectNid: OBJ_create " + getLastOpenSslError () };
	}

	return result;
}

const ExpectedFailuresMap & getAutomaticVerificationExpectedFailures ()
{
	static ExpectedFailuresMap result{
		{ 1,
		{ X509_V_ERR_CERT_SIGNATURE_FAILURE } },
		{ 2,
		{ X509_V_ERR_CERT_SIGNATURE_FAILURE, X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN } }
	};

	return result;
}

void setCaPublicKeyValidator (const SslPtrView & ssl, CaPublicKeyValidator & validator)
{
	if (-1 == SSL_set_ex_data (*ssl, getSslExDataIndex (), &validator))
	{
		throw std::runtime_error{ "setCaPublicKeyValidator: SSL_set_ex_data " + getLastOpenSslError () };
	}
}

std::ifstream openFileForReading (const boost::filesystem::path & filePath, const std::ios_base::openmode mode)
{
	std::ifstream fileStream{ filePath.string (), mode };
	if (!fileStream)
	{
		throw std::runtime_error{ "openFileForReading: unable to open " + filePath.string () };
	}

	return fileStream;
}

std::string readFromFile (const boost::filesystem::path & filePath)
{
	auto stream = openFileForReading (filePath, std::ios_base::in);
	return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

EvpPkeyPtr parsePrivateKeyFromPemFile (const boost::filesystem::path & filePath)
{
	return parseFromPemFile<PEM_read_bio_PrivateKey, EvpPkeyPtr> (filePath);
}

EvpPkeyPtr parsePublicKeyFromPemFile (const boost::filesystem::path & filePath)
{
	return parseFromPemFile<PEM_read_bio_PUBKEY, EvpPkeyPtr> (filePath);
}

X509Ptr parseCertificateFromPemFile (const boost::filesystem::path & filePath)
{
	return parseFromPemFile<PEM_read_bio_X509, X509Ptr> (filePath);
}

std::ofstream openFileForWriting (const boost::filesystem::path & filePath, const std::ios_base::openmode mode)
{
	std::ofstream fileStream{ filePath.string (), mode };
	if (!fileStream)
	{
		throw std::runtime_error{ "openFileForWriting: unable to open " + filePath.string () };
	}

	return fileStream;
}

void writeToFile (const std::string_view & data, const boost::filesystem::path & filePath)
{
	auto stream = openFileForWriting (filePath, std::ios_base::out);
	stream << data;
}

void serializePrivateKeyIntoPemFile (const EvpPkeyPtrView & privateKey, const boost::filesystem::path & filePath)
{
	return serializeIntoPemFile (
	privateKey,
	std::bind (
	PEM_write_bio_PKCS8PrivateKey,
	std::placeholders::_1,
	std::placeholders::_2,
	nullptr,
	nullptr,
	0,
	nullptr,
	nullptr),
	filePath);
}

void serializePublicKeyIntoPemFile (const EvpPkeyPtrView & publicKey, const boost::filesystem::path & filePath)
{
	return serializeIntoPemFile (publicKey, PEM_write_bio_PUBKEY, filePath);
}

void serializeCertificateIntoPemFile (const X509PtrView & certificate, const boost::filesystem::path & filePath)
{
	return serializeIntoPemFile (certificate, PEM_write_bio_X509, filePath);
}

CertificateSignature getCertificateSignatureImpl (const X509PtrView & certificate)
{
	AlgorithmPtrView algorithm{};
	Asn1BitStringPtrView signature{};

	X509_get0_signature (
	const_cast<const ASN1_BIT_STRING **> (signature.getAddress ()),
	const_cast<const X509_ALGOR **> (algorithm.getAddress ()),
	*certificate);

	if (!algorithm || !signature)
	{
		throw std::runtime_error{ "getCertificateSignatureImpl: X509_get0_signature: " + getLastOpenSslError () };
	}

	return std::make_pair (std::move (algorithm), std::move (signature));
}

void setAlgorithmTypeToEd25519 (const AlgorithmPtrView & algorithm)
{
	const auto algorithmObject = Asn1ObjectPtr::make (OBJ_nid2obj (NID_ED25519));
	if (1 != X509_ALGOR_set0 (*algorithm, *algorithmObject, V_ASN1_UNDEF, nullptr))
	{
		throw std::runtime_error{ "setAlgorithmTypeToEd25519: X509_ALGOR_set0: " + getLastOpenSslError () };
	}
}

void addCertificateCustomSignature (const X509PtrView & certificate, const BufferView & signature)
{
	const auto [oldAlgorithm, oldSignature] = getCertificateSignatureImpl (certificate);
	setAlgorithmTypeToEd25519 (oldAlgorithm);

	if (1 != ASN1_BIT_STRING_set (*oldSignature, const_cast<std::uint8_t *> (signature.getData ()), static_cast<int> (signature.getSize ())))
	{
		throw std::runtime_error{ "addCertificateCustomSignature: ASN1_BIT_STRING_set: " + getLastOpenSslError () };
	}
}

EvpPkeyPtr getEvpPublicKeyFromCustomPublicKey (const BufferView & publicKey)
{
	auto result = EvpPkeyPtr::make (EVP_PKEY_new_raw_public_key (
	NID_ED25519,
	nullptr,
	publicKey.getData (),
	publicKey.getSize ()));

	if (!result)
	{
		throw std::runtime_error{ "getEvpPublicKeyFromCustomPublicKey: EVP_PKEY_new_raw_public_key: " + getLastOpenSslError () };
	}

	return result;
}

Buffer getCustomPublicKeyFromEvpPublicKey (const EvpPkeyPtrView & publicKey)
{
	std::size_t publicKeySize{};
	if (1 != EVP_PKEY_get_raw_public_key (*publicKey, nullptr, &publicKeySize))
	{
		throw std::runtime_error{ "getCustomPublicKeyFromEvpPublicKey: EVP_PKEY_get_raw_public_key: " + getLastOpenSslError () };
	}

	if (publicKeySize != ED25519_PUBLIC_KEY_SIZE)
	{
		throw std::runtime_error{ "getCustomPublicKeyFromEvpPublicKey: unexpected public key size" };
	}

	Buffer result (publicKeySize);
	if (1 != EVP_PKEY_get_raw_public_key (*publicKey, result.data (), &publicKeySize))
	{
		throw std::runtime_error{ "getCustomPublicKeyFromEvpPublicKey: EVP_PKEY_get_raw_public_key: " + getLastOpenSslError () };
	}

	return result;
}

Buffer getCustomPublicKeyFromCertificate (const X509PtrView & certificate)
{
	const auto publicKey = EvpPkeyPtrView::make (X509_get0_pubkey (*certificate));
	if (!publicKey)
	{
		throw std::runtime_error{ "getCustomPublicKeyFromCertificate: X509_get0_pubkey: " + getLastOpenSslError () };
	}

	return getCustomPublicKeyFromEvpPublicKey (publicKey);
}

Asn1OctetStringPtr createAsn1OctetString (const BufferView & data)
{
	return createAsn1SpecificType<ASN1_OCTET_STRING_set, Asn1OctetStringPtr> (data);
}

Asn1BitStringPtr createAsn1BitString (const BufferView & data)
{
	return createAsn1SpecificType<ASN1_BIT_STRING_set, Asn1BitStringPtr> (data);
}

Asn1TypePtr createAsn1GenericTypeFromObject (const Asn1ObjectPtrView & object)
{
	return createAsn1GenericType (*object, V_ASN1_OBJECT);
}

Asn1TypePtr createAsn1GenericTypeFromBitString (const Asn1BitStringPtrView & bitString)
{
	return createAsn1GenericType (*bitString, V_ASN1_BIT_STRING);
}

Asn1TypePtr createAsn1GenericTypeFromSequence (const Asn1OctetStringPtrView & sequence)
{
	return createAsn1GenericType (*sequence, V_ASN1_SEQUENCE);
}

Asn1TypePtr createAsn1GenericTypeFromRawBitString (const BufferView & data)
{
	const auto bitString = createAsn1BitString (data);
	return createAsn1GenericTypeFromBitString (bitString);
}

Asn1TypePtr createAsn1GenericTypeFromPublicKey (const BufferView & publicKey)
{
	return createAsn1GenericTypeFromRawBitString (publicKey);
}

Asn1TypePtr createAsn1GenericTypeFromSignature (const BufferView & signature)
{
	return createAsn1GenericTypeFromRawBitString (signature);
}

Asn1TypePtr createAsn1GenericTypeFromEd25519Algorithm ()
{
	auto algorithmObject = Asn1ObjectPtr::make (OBJ_nid2obj (NID_ED25519));
	if (!algorithmObject)
	{
		throw std::runtime_error{ "createAsn1GenericTypeFromEd25519Algorithm: OBJ_nid2obj: " + getLastOpenSslError () };
	}

	return createAsn1GenericTypeFromObject (algorithmObject);
}

Asn1SequencePtr createAsn1SequenceFromGenericTypes (const std::vector<Asn1TypePtrView> & genericTypes)
{
	std::size_t resultSize{};
	auto result = Asn1SequencePtr::make ();
	for (const auto & genericType : genericTypes)
	{
		if (++resultSize != sk_ASN1_TYPE_push (*result, *genericType))
		{
			throw std::runtime_error{ "createAsn1SequenceFromGenericTypes: sk_ASN1_TYPE_push: " + getLastOpenSslError () };
		}
	}

	return result;
}

Buffer serializeCertificateDataToBeSignedIntoAsn1 (const X509PtrView & certificate)
{
	return serializeIntoAsn1<i2d_re_X509_tbs> (certificate);
}

Buffer serializeSequenceIntoAsn1 (const Asn1SequencePtrView & sequence)
{
	return serializeIntoAsn1<i2d_ASN1_SEQUENCE_ANY> (sequence);
}

Buffer serializeAdditionalSignatureIntoAsn1 (
const Asn1TypePtrView & algorithmGenericType,
const AdditionalSignature & additionalSignature)
{
	const auto publicKeyGenericType = createAsn1GenericTypeFromPublicKey (additionalSignature.getPublicKey ());
	const auto signatureGenericType = createAsn1GenericTypeFromSignature (additionalSignature.getSignature ());

	const auto sequence = createAsn1SequenceFromGenericTypes (std::vector<Asn1TypePtrView>{
	algorithmGenericType,
	publicKeyGenericType,
	signatureGenericType });

	return serializeSequenceIntoAsn1 (sequence);
}

std::vector<Buffer> serializeInnerAdditionalSignaturesIntoAsn1 (
const std::vector<AdditionalSignature> & additionalSignatures)
{
	const auto algorithmGenericType = createAsn1GenericTypeFromEd25519Algorithm ();

	std::vector<Buffer> result (additionalSignatures.size ());
	std::transform (
	additionalSignatures.cbegin (),
	additionalSignatures.cend (),
	result.begin (),
	std::bind (
	serializeAdditionalSignatureIntoAsn1,
	std::cref (algorithmGenericType),
	std::placeholders::_1));

	return result;
}

Buffer serializeAdditionalSignaturesIntoAsn1 (const std::vector<AdditionalSignature> & additionalSignatures)
{
	const auto innerAdditionalSignatures = serializeInnerAdditionalSignaturesIntoAsn1 (additionalSignatures);

	std::vector<Asn1TypePtr> innerAdditionalSignaturesGenericTypes (additionalSignatures.size ());
	std::transform (
	innerAdditionalSignatures.cbegin (),
	innerAdditionalSignatures.cend (),
	innerAdditionalSignaturesGenericTypes.begin (),
	std::bind (
	createAsn1GenericTypeFromSequence,
	std::bind (
	createAsn1OctetString,
	std::placeholders::_1)));

	const std::vector<Asn1TypePtrView> innerAdditionalSignaturesGenericTypesViews{
		innerAdditionalSignaturesGenericTypes.cbegin (),
		innerAdditionalSignaturesGenericTypes.cend ()
	};
	const auto sequence = createAsn1SequenceFromGenericTypes (innerAdditionalSignaturesGenericTypesViews);

	return serializeSequenceIntoAsn1 (sequence);
}

void addCertificateAdditionalSignatures (const X509PtrView & certificate, const std::vector<AdditionalSignature> & additionalSignatures)
{
	const auto serializedAdditionalSignatures = serializeAdditionalSignaturesIntoAsn1 (additionalSignatures);
	const auto extensionObjectNid = getAdditionalSignaturesExtensionObjectNid ();

	const auto extensionValue = createAsn1OctetString (serializedAdditionalSignatures);
	const auto extension = X509ExtensionPtr::make (X509_EXTENSION_create_by_NID (nullptr, extensionObjectNid, 0, *extensionValue));

	if (1 != X509_add_ext (*certificate, *extension, -1))
	{
		throw std::runtime_error{ "addCertificateAdditionalSignatures: X509_add_ext: " + getLastOpenSslError () };
	}
}

X509ExtensionPtr removeAndGetCertificateAdditionalSignaturesExtension (const X509PtrView & certificate)
{
	const auto extensionObjectNid = getAdditionalSignaturesExtensionObjectNid ();
	const auto extensionIndex = X509_get_ext_by_NID (*certificate, extensionObjectNid, -1);
	if (-1 == extensionIndex)
	{
		return X509ExtensionPtr{};
	}

	return X509ExtensionPtr::make (X509_delete_ext (*certificate, extensionIndex));
}

Buffer getCertificateDataToBeSigned (const X509PtrView & certificate)
{
	CertificateDataToBeSignedCleaner cleaner{ certificate };
	return serializeCertificateDataToBeSignedIntoAsn1 (certificate);
}

void setCertificateSubject (const X509PtrView & certificate, const std::string_view & subject)
{
	setCertificateName<X509_get_subject_name> (certificate, subject);
}

void setCertificateIssuer (const X509PtrView & certificate, const std::string_view & issuer)
{
	setCertificateName<X509_get_issuer_name> (certificate, issuer);
}

X509Ptr generateCertificate (
const std::string_view & subject,
const std::string_view & issuer,
const std::uint64_t serialNumber,
const std::chrono::seconds & validity,
const EvpPkeyPtrView & publicKey)
{
	auto result = X509Ptr::make ();
	setCertificateSubject (result, subject);
	setCertificateIssuer (result, issuer);

	if (1 != X509_set_version (*result, X509_CERTIFICATES_VERSION))
	{
		throw std::runtime_error{ "generateCertificate: X509_set_version: " + getLastOpenSslError () };
	}

	const auto x509SerialNumber = Asn1IntegerPtrView::make (X509_get_serialNumber (*result));
	if (1 != ASN1_INTEGER_set (*x509SerialNumber, static_cast<long> (serialNumber)))
	{
		throw std::runtime_error{ "generateCertificate: ASN1_INTEGER_set: " + getLastOpenSslError () };
	}

	const auto notBefore = Asn1TimePtrView::make (X509_get_notBefore (*result));
	static_cast<void> (X509_gmtime_adj (*notBefore, 0));

	const auto notAfter = Asn1TimePtrView::make (X509_get_notAfter (*result));
	static_cast<void> (X509_gmtime_adj (*notAfter, validity.count ()));

	if (1 != X509_set_pubkey (*result, *publicKey))
	{
		throw std::runtime_error{ "generateCertificate: X509_set_pubkey: " + getLastOpenSslError () };
	}

	return result;
}

void verifyCertificateSigningAlgorithm (const X509PtrView & certificate)
{
	const auto algorithmNid = X509_get_signature_nid (*certificate);
	if (NID_undef == algorithmNid)
	{
		throw std::runtime_error{ "verifyCertificateSigningAlgorithm: X509_get_signature_nid: " + getLastOpenSslError () };
	}

	if (NID_ED25519 != algorithmNid)
	{
		throw std::runtime_error{ "verifyCertificateSigningAlgorithm: algorithm used for signing is not ed25519" };
	}
}

void verifyCertificateNormalSignature (const X509PtrView & certificate, const EvpPkeyPtrView & publicKey)
{
	verifyCertificateSigningAlgorithm (certificate);

	if (1 != X509_verify (*certificate, *publicKey))
	{
		throw std::runtime_error{ "verifyCertificateNormalSignature: X509_verify: bad signature" };
	}
}

BufferView getCertificateSignature (const X509PtrView & certificate)
{
	const auto [_, signature] = getCertificateSignatureImpl (certificate);

	const auto result = ConstBufferPtrView::make (ASN1_STRING_get0_data (*signature));
	const auto resultSize = ASN1_STRING_length (*signature);
	if (1 > resultSize)
	{
		throw std::runtime_error{ "getCertificateSignature: ASN1_STRING_length: " + getLastOpenSslError () };
	}

	return BufferView (*result, resultSize); // NOLINT(modernize-return-braced-init-list)
}

void verifyCustomSignature (const BufferView & dataToBeVerified, const BufferView & publicKey, const BufferView & signature)
{
	if (publicKey.getSize () != ED25519_PUBLIC_KEY_SIZE)
	{
		throw std::runtime_error{ "verifyCustomSignature: unexpected public key size" };
	}

	if (signature.getSize () != ED25519_SIGNATURE_SIZE)
	{
		// TODO: the signature seems to come in as 63 bytes every now and then. investigate where the off by 1 error comes from and fix it

		throw std::runtime_error{ "verifyCustomSignature: unexpected signature size: " + std::to_string (signature.getSize ()) };
	}

	if (0 != ed25519_sign_open (dataToBeVerified.getData (), dataToBeVerified.getSize (), publicKey.getData (), signature.getData ()))
	{
		throw std::runtime_error{ "verifyCustomSignature: bad signature" };
	}
}

void verifyCertificateCustomSignature (
const X509PtrView & certificate,
const BufferView & publicKey,
const std::optional<BufferView> & dataToBeVerified)
{
	verifyCertificateSigningAlgorithm (certificate);

	verifyCustomSignature (
	dataToBeVerified.has_value () ? *dataToBeVerified
								  : getCertificateDataToBeSigned (certificate),
	publicKey,
	getCertificateSignature (certificate));
}

void addIsCaExtension (const X509PtrView & certificate, const X509V3Ctx & extensionContext, bool isCa)
{
	const auto basicConstraintsExtensionValue = std::string{ "critical, CA:" } + (isCa ? "TRUE" : "FALSE");

	auto extension = X509ExtensionPtr::make (X509V3_EXT_conf_nid (
	nullptr,
	extensionContext.breakConst (),
	NID_basic_constraints,
	basicConstraintsExtensionValue.c_str ()));

	if (1 != X509_add_ext (*certificate, *extension, -1))
	{
		throw std::runtime_error{ "addIsCaExtension: X509_add_ext: " + getLastOpenSslError () };
	}

	if (isCa)
	{
		extension = X509ExtensionPtr::make (X509V3_EXT_conf_nid (
		nullptr,
		extensionContext.breakConst (),
		NID_key_usage,
		"critical, keyCertSign"));

		if (1 != X509_add_ext (*certificate, *extension, -1))
		{
			throw std::runtime_error{ "addIsCaExtension: X509_add_ext: " + getLastOpenSslError () };
		}
	}
}

Buffer getCustomPublicKeyFromCustomPrivateKey (const BufferView & privateKey)
{
	if (privateKey.getSize () != ED25519_PRIVATE_KEY_SIZE)
	{
		throw std::runtime_error{ "getCustomPublicKeyFromCustomPrivateKey: unexpected private key size" };
	}

	Buffer result (ED25519_PUBLIC_KEY_SIZE);
	ed25519_publickey (privateKey.getData (), result.data ());

	return result;
}

Buffer createCustomSignature (const BufferView & privateKey, const BufferView & dataToBeSigned)
{
	const auto publicKey = getCustomPublicKeyFromCustomPrivateKey (privateKey);

	Buffer result (ED25519_SIGNATURE_SIZE);
	ed25519_sign (
	dataToBeSigned.getData (),
	dataToBeSigned.getSize (),
	privateKey.getData (),
	publicKey.data (),
	result.data ());

	return result;
}

EvpPkeyPtr generatePrivateKey ()
{
	const auto evpPkeyContext = EvpPkeyCtxPtr::make (EVP_PKEY_CTX_new_id (NID_ED25519, nullptr));
	if (!evpPkeyContext)
	{
		throw std::runtime_error{ "generatePrivateKey: EVP_PKEY_CTX_new_id: " + getLastOpenSslError () };
	}

	if (1 != EVP_PKEY_keygen_init (*evpPkeyContext))
	{
		throw std::runtime_error{ "generatePrivateKey: EVP_PKEY_keygen_init: " + getLastOpenSslError () };
	}

	auto result = EvpPkeyPtr::make ();
	if (1 != EVP_PKEY_keygen (*evpPkeyContext, result.getAddress ()))
	{
		throw std::runtime_error{ "generatePrivateKey: EVP_PKEY_keygen: " + getLastOpenSslError () };
	}

	return result;
}

EvpPkeyPtr generatePrivateKeyAndSave (const boost::filesystem::path & privatePemFile, const boost::filesystem::path & publicPemFile)
{
	auto result = generatePrivateKey ();

	serializePrivateKeyIntoPemFile (result, privatePemFile);
	serializePublicKeyIntoPemFile (result, publicPemFile);

	return result;
}

void addCertificateFakeSignature (const X509PtrView & certificate)
{
	const auto randomPrivateKey = generatePrivateKey ();
	if (1 > X509_sign (*certificate, *randomPrivateKey, nullptr))
	{
		throw std::runtime_error{ "addCertificateFakeSignature: X509_sign: " + getLastOpenSslError () };
	}
}

std::vector<AdditionalSignature> createDummyAdditionalSignatures (
const BufferView & dataToBeSigned,
const BufferView & privateKey,
const BufferView & publicKey)
{
	const auto signature = createCustomSignature (privateKey, dataToBeSigned);

	std::vector<AdditionalSignature> result{};
	for (std::size_t itr = 0; itr != ADDITIONAL_SIGNATURES_DUMMY_COUNT; ++itr)
	{
		result.emplace_back (Buffer{ publicKey }, signature);
	}

	return result;
}

void signCaCertificate (const X509PtrView & certificate, const BufferView & privateKey, const BufferView & publicKey)
{
	addCertificateFakeSignature (certificate);
	const auto dataToBeSigned = getCertificateDataToBeSigned (certificate);

	const auto customSignature = createCustomSignature (privateKey, dataToBeSigned);
	addCertificateCustomSignature (certificate, customSignature);

	const auto additionalSignatures = createDummyAdditionalSignatures (dataToBeSigned, privateKey, publicKey);
	addCertificateAdditionalSignatures (certificate, additionalSignatures);
}

Asn1SequencePtr parseSequenceFromAsn1 (const BufferView & data)
{
	return parseFromAsn1<d2i_ASN1_SEQUENCE_ANY, Asn1SequencePtr> (data);
}

std::vector<AdditionalSignature> getCertificateAdditionalSignatures (const X509ExtensionPtrView & additionalSignaturesExtension)
{
	// TODO: refactor this method

	const auto sequenceData1 = Asn1OctetStringPtrView::make (X509_EXTENSION_get_data (*additionalSignaturesExtension));
	const auto sequenceStringData1 = ConstBufferPtrView::make (ASN1_STRING_get0_data (*sequenceData1));
	const auto sequenceStringDataSize1 = ASN1_STRING_length (*sequenceData1);
	if (1 > sequenceStringDataSize1)
	{
		throw std::runtime_error{ "getCertificateAdditionalSignatures1: " + getLastOpenSslError () };
	}

	const auto sequence1 = parseSequenceFromAsn1 (BufferView (*sequenceStringData1, sequenceStringDataSize1));
	const auto sequenceSize1 = sk_ASN1_TYPE_num (*sequence1);

	std::vector<AdditionalSignature> result{};
	result.reserve (sequenceSize1);

	for (std::size_t itr = 0; itr != sequenceSize1; ++itr)
	{
		const auto elementGenericType = Asn1TypePtr::make (sk_ASN1_TYPE_value (*sequence1, static_cast<int> (itr)));
		if (ASN1_TYPE_get (*elementGenericType) != V_ASN1_SEQUENCE)
		{
			throw std::runtime_error{ "getCertificateAdditionalSignatures2: " + getLastOpenSslError () };
		}

		const auto sequenceData2 = Asn1OctetStringPtrView::make (elementGenericType.get ()->value.sequence);
		const auto sequenceStringData2 = ConstBufferPtrView::make (ASN1_STRING_get0_data (*sequenceData2));
		const auto sequenceStringDataSize2 = ASN1_STRING_length (*sequenceData2);
		if (1 > sequenceStringDataSize2)
		{
			throw std::runtime_error{ "getCertificateAdditionalSignatures3: " + getLastOpenSslError () };
		}

		const auto sequence2 = parseSequenceFromAsn1 (BufferView (*sequenceStringData2, sequenceStringDataSize2));
		const auto sequenceSize2 = sk_ASN1_TYPE_num (*sequence2);

		if (3 != sequenceSize2)
		{
			throw std::runtime_error{ "getCertificateAdditionalSignatures4: " + getLastOpenSslError () };
		}

		const auto algorithmGenericType = Asn1TypePtr::make (sk_ASN1_TYPE_value (*sequence2, 0));
		if (ASN1_TYPE_get (*algorithmGenericType) != V_ASN1_OBJECT)
		{
			throw std::runtime_error{ "getCertificateAdditionalSignatures5: " + getLastOpenSslError () };
		}

		const auto algorithmObject = Asn1ObjectPtrView::make (algorithmGenericType.get ()->value.object);
		if (NID_ED25519 != OBJ_obj2nid (*algorithmObject))
		{
			throw std::runtime_error{ "getCertificateAdditionalSignatures6: " + getLastOpenSslError () };
		}

		const auto publicKeyGenericType = Asn1TypePtr::make (sk_ASN1_TYPE_value (*sequence2, 1));
		if (ASN1_TYPE_get (*publicKeyGenericType) != V_ASN1_BIT_STRING)
		{
			throw std::runtime_error{ "getCertificateAdditionalSignatures7: " + getLastOpenSslError () };
		}

		const auto publicKeyBitString = Asn1BitStringPtrView::make (publicKeyGenericType.get ()->value.bit_string);
		const auto publicKeyData = ConstBufferPtrView::make (ASN1_STRING_get0_data (*publicKeyBitString));
		const auto publicKeyDataSize = ASN1_STRING_length (*publicKeyBitString);
		if (1 > publicKeyDataSize)
		{
			throw std::runtime_error{ "getCertificateAdditionalSignatures8: " + getLastOpenSslError () };
		}

		const auto signatureGenericType = Asn1TypePtr::make (sk_ASN1_TYPE_value (*sequence2, 2));
		if (ASN1_TYPE_get (*signatureGenericType) != V_ASN1_BIT_STRING)
		{
			throw std::runtime_error{ "getCertificateAdditionalSignatures9: " + getLastOpenSslError () };
		}

		const auto signatureBitString = Asn1BitStringPtrView::make (signatureGenericType.get ()->value.bit_string);
		const auto signatureData = ConstBufferPtrView::make (ASN1_STRING_get0_data (*signatureBitString));
		const auto signatureDataSize = ASN1_STRING_length (*signatureBitString);
		if (1 > signatureDataSize)
		{
			throw std::runtime_error{ "getCertificateAdditionalSignatures10: " + getLastOpenSslError () };
		}

		result.emplace_back (
		Buffer{ *publicKeyData, *publicKeyData + publicKeyDataSize },
		Buffer{ *signatureData, *signatureData + signatureDataSize });
	}

	return result;
}

std::optional<std::vector<Buffer>> getPublicKeysFromAdditionalSignatures (std::vector<AdditionalSignature> & additionalSignatures)
{
	if (additionalSignatures.empty ())
	{
		return std::nullopt;
	}

	std::vector<Buffer> result (additionalSignatures.size ());
	std::transform (
	std::make_move_iterator (additionalSignatures.begin ()),
	std::make_move_iterator (additionalSignatures.end ()),
	result.begin (),
	[] (auto && additionalSignature) {
		return std::move (additionalSignature.getPublicKey ());
	});

	return std::move (result);
}

VerifiedCertificateSignatures verifyCertificateAdditionalSignatures (const X509PtrView & certificate)
{
	CertificateDataToBeSignedCleaner cleaner{ certificate };
	VerifiedCertificateSignatures result{ serializeCertificateDataToBeSignedIntoAsn1 (certificate) };

	const auto & additionalSignaturesExtension = cleaner.getAdditionalSignaturesExtension ();
	if (!additionalSignaturesExtension)
	{
		return result;
	}

	auto additionalSignatures = getCertificateAdditionalSignatures (additionalSignaturesExtension);
	for (const auto & additionalSignature : additionalSignatures)
	{
		verifyCustomSignature (
		result.getDataToBeVerified (),
		additionalSignature.getPublicKey (),
		additionalSignature.getSignature ());
	}

	auto additionalSignaturesPublicKeys = getPublicKeysFromAdditionalSignatures (additionalSignatures);
	if (additionalSignaturesPublicKeys.has_value ())
	{
		result.setAdditionalSignaturesPublicKeys (std::move (*additionalSignaturesPublicKeys));
	}

	return result;
}

VerifiedCertificateSignatures verifyCaCertificate (const X509PtrView & certificate)
{
	auto result = verifyCertificateAdditionalSignatures (certificate);

	auto publicKey = getCustomPublicKeyFromCertificate (certificate);
	verifyCertificateCustomSignature (certificate, publicKey, result.getDataToBeVerified ());

	result.setPublicKey (std::move (publicKey));
	return result;
}

void markCertificateAsCa (const X509PtrView & certificate, const X509V3Ctx & extensionContext)
{
	addIsCaExtension (certificate, extensionContext, true);
}

void generateCaCertificate (key_group const & key_group, boost::filesystem::path const & resources_dir)
{
	const auto publicKey = getCaPublicKey (key_group);
	const auto evpPublicKey = getEvpPublicKeyFromCustomPublicKey (publicKey);

	const auto certificate = generateCertificate (
	CA_CERTIFICATE_NAME,
	CA_CERTIFICATE_NAME,
	CA_CERTIFICATE_SERIAL_NUMBER,
	std::chrono::seconds{ CA_CERTIFICATE_VALIDITY_SECONDS },
	evpPublicKey);

	X509V3Ctx extensionContext{ certificate, certificate };
	markCertificateAsCa (certificate, extensionContext);

	signCaCertificate (certificate, getCaPrivateKey (key_group), getCaPublicKey (key_group));
	verifyCaCertificate (certificate);

	serializeCertificateIntoPemFile (certificate, resources_dir / CA_CERTIFICATE_PEM_FILE);
}

void signIntermediateCertificate (const X509PtrView & certificate, const BufferView & privateKey)
{
	addCertificateFakeSignature (certificate);
	const auto dataToBeSigned = getCertificateDataToBeSigned (certificate);

	const auto customSignature = createCustomSignature (privateKey, dataToBeSigned);
	addCertificateCustomSignature (certificate, customSignature);
}

void generateIntermediateCertificate (key_group const & key_group, boost::filesystem::path const & resources_dir)
{
	const auto publicKey = parsePublicKeyFromPemFile (resources_dir / INTERMEDIATE_PUBLIC_KEY_PEM_FILE);
	const auto certificate = generateCertificate (
	INTERMEDIATE_CERTIFICATE_NAME,
	CA_CERTIFICATE_NAME,
	INTERMEDIATE_CERTIFICATE_SERIAL_NUMBER,
	std::chrono::seconds{ INTERMEDIATE_CERTIFICATE_VALIDITY_SECONDS },
	publicKey);

	const auto issuer = parseCertificateFromPemFile (resources_dir / CA_CERTIFICATE_PEM_FILE);
	X509V3Ctx extensionContext{ issuer, certificate };
	markCertificateAsCa (certificate, extensionContext);

	const auto issuerPrivateKey = getCaPrivateKey (key_group);
	signIntermediateCertificate (certificate, issuerPrivateKey);

	const auto issuerPublicKey = getCaPublicKey (key_group);
	verifyCertificateCustomSignature (certificate, issuerPublicKey);

	serializeCertificateIntoPemFile (certificate, resources_dir / INTERMEDIATE_CERTIFICATE_PEM_FILE);
}

void signLeafCertificate (const X509PtrView & certificate, const EvpPkeyPtrView & privateKey)
{
	if (1 > X509_sign (*certificate, *privateKey, nullptr))
	{
		throw std::runtime_error{ "signLeafCertificate: X509_sign: " + getLastOpenSslError () };
	}
}

void markCertificateAsLeaf (const X509PtrView & certificate, const X509V3Ctx & extensionContext)
{
	addIsCaExtension (certificate, extensionContext, false);

	auto extension = X509ExtensionPtr::make (X509V3_EXT_conf_nid (
	nullptr,
	extensionContext.breakConst (),
	NID_key_usage,
	"critical, digitalSignature, keyAgreement"));

	if (1 != X509_add_ext (*certificate, *extension, -1))
	{
		throw std::runtime_error{ "markCertificateAsLeaf: X509_add_ext: " + getLastOpenSslError () };
	}

	extension = X509ExtensionPtr::make (X509V3_EXT_conf_nid (
	nullptr,
	extensionContext.breakConst (),
	NID_ext_key_usage,
	"critical, clientAuth, serverAuth"));

	if (1 != X509_add_ext (*certificate, *extension, -1))
	{
		throw std::runtime_error{ "markCertificateAsLeaf: X509_add_ext: " + getLastOpenSslError () };
	}
}

void generateLeafCertificate (const boost::filesystem::path & resources_dir)
{
	const auto publicKey = parsePublicKeyFromPemFile (resources_dir / LEAF_PUBLIC_KEY_PEM_FILE);

	const auto certificate = generateCertificate (
	LEAF_CERTIFICATE_NAME,
	INTERMEDIATE_CERTIFICATE_NAME,
	LEAF_CERTIFICATE_SERIAL_NUMBER,
	std::chrono::seconds{ LEAF_CERTIFICATE_VALIDITY_SECONDS },
	publicKey);

	const auto issuer = parseCertificateFromPemFile (resources_dir / INTERMEDIATE_CERTIFICATE_PEM_FILE);
	X509V3Ctx extensionContext{ issuer, certificate };
	markCertificateAsLeaf (certificate, extensionContext);

	const auto issuerPrivateKey = parsePrivateKeyFromPemFile (resources_dir / INTERMEDIATE_PRIVATE_KEY_PEM_FILE);
	signLeafCertificate (certificate, issuerPrivateKey);

	const auto issuerPublicKey = parsePublicKeyFromPemFile (resources_dir / INTERMEDIATE_PUBLIC_KEY_PEM_FILE);
	verifyCertificateNormalSignature (certificate, issuerPublicKey);

	serializeCertificateIntoPemFile (certificate, resources_dir / LEAF_CERTIFICATE_PEM_FILE);
}

void composeCertificateChainPemFile (const boost::filesystem::path & resources_dir)
{
	const auto leafCertificatePem = readFromFile (resources_dir / LEAF_CERTIFICATE_PEM_FILE);
	const auto intermediateCertificatePem = readFromFile (resources_dir / INTERMEDIATE_CERTIFICATE_PEM_FILE);
	const auto caCertificatePem = readFromFile (resources_dir / CA_CERTIFICATE_PEM_FILE);

	writeToFile (leafCertificatePem + intermediateCertificatePem + caCertificatePem, resources_dir / CERTIFICATES_CHAIN_PEM_FILE);
}

void createResourcesDirectory (const boost::filesystem::path & resources_dir)
{
	if (!boost::filesystem::exists (resources_dir))
	{
		boost::filesystem::create_directory (resources_dir);
	}
}

void generatePki (key_group const & key_group, boost::filesystem::path const & certificate_dir)
{
	createResourcesDirectory (certificate_dir);

	generateCaCertificate (key_group, certificate_dir); // this CA certificate (meaning ROOT certificate) has as private key the node private key

	static_cast<void> (generatePrivateKeyAndSave (certificate_dir / INTERMEDIATE_PRIVATE_KEY_PEM_FILE, certificate_dir / INTERMEDIATE_PUBLIC_KEY_PEM_FILE));
	generateIntermediateCertificate (key_group, certificate_dir);

	static_cast<void> (generatePrivateKeyAndSave (certificate_dir / LEAF_PRIVATE_KEY_PEM_FILE, certificate_dir / LEAF_PUBLIC_KEY_PEM_FILE));
	generateLeafCertificate (certificate_dir);

	composeCertificateChainPemFile (certificate_dir);
}

std::string getCertificateSubject (const X509PtrView & certificate)
{
	const auto bioMethod = ConstBioMethodPtr::make (BIO_s_mem ());
	const auto bio = BioPtr::make (BIO_new (*bioMethod));

	const auto subject = X509NamePtrView::make (X509_get_subject_name (*certificate));
	if (1 != X509_NAME_print_ex (*bio, *subject, 1, 0))
	{
		throw std::runtime_error{ "getCertificateSubject: X509_NAME_print_ex: " + getLastOpenSslError () };
	}

	return readFromBio (bio);
}

void checkAutomaticVerificationFailureWasExpected (int depth, int error)
{
	const auto & expectedFailures = getAutomaticVerificationExpectedFailures ();

	const auto depthItr = expectedFailures.find (depth);
	if (depthItr == expectedFailures.cend ())
	{
		throw std::runtime_error{ "checkAutomaticVerificationFailureWasExpected: unexpected depth = " + std::to_string (depth) };
	}

	const auto & errors = depthItr->second;
	const auto errorItr = std::find (errors.cbegin (), errors.cend (), error);
	if (errorItr == errors.cend ())
	{
		throw std::runtime_error{ "checkAutomaticVerificationFailureWasExpected: unexpected failure = "
			+ std::to_string (error) + " at depth = " + std::to_string (depth) };
	}
}

void validateVerifiedCaCertificateSignatures (
const VerifiedCertificateSignatures & verifiedCaCertificateSignatures,
const CaPublicKeyValidator & validator)
{
	const auto & caPublicKeyOpt = verifiedCaCertificateSignatures.getPublicKey ();
	if (!caPublicKeyOpt.has_value ())
	{
		throw std::runtime_error{ "validateVerifiedCaCertificateSignatures: missing CA public key" };
	}

	static_cast<void> (validator (*caPublicKeyOpt, std::nullopt));

	const auto & additionalSignaturesPublicKeysOpt = verifiedCaCertificateSignatures.getAdditionalSignaturesPublicKeys ();
	if (!additionalSignaturesPublicKeysOpt.has_value ())
	{
		throw std::runtime_error{ "validateVerifiedCaCertificateSignatures: missing CA additional signatures public keys" };
	}

	for (const auto & publicKey : *additionalSignaturesPublicKeysOpt)
	{
		static_cast<void> (validator (publicKey, std::nullopt));
	}
}

void doManualVerificationForCaCertificate (const X509PtrView & certificate, const CaPublicKeyValidator & validator)
{
	const auto & caCertificate = validator (std::nullopt, std::nullopt);
	if (caCertificate.has_value ())
	{
		if (0 != X509_cmp (*certificate, **caCertificate))
		{
			throw std::runtime_error{ "doManualVerificationForCaCertificate: "
									  "unexpected CA certificate after having seen a different one before" };
		}
	}
	else
	{
		const auto additionalSignaturesPublicKeys = verifyCaCertificate (certificate);
		validateVerifiedCaCertificateSignatures (additionalSignaturesPublicKeys, validator);

		auto newCaCertificate = X509Ptr::make (X509_dup (*certificate));
		static_cast<void> (validator (std::nullopt, std::move (newCaCertificate)));
	}
}

void doManualVerificationForIntermediateCertificate (const X509PtrView & certificate, const CaPublicKeyValidator & validator)
{
	const auto & caCertificate = validator (std::nullopt, std::nullopt);
	if (!caCertificate.has_value ())
	{
		throw std::runtime_error{ "doManualVerificationForIntermediateCertificate: "
								  "no CA certificate seen yet, cannot proceed with verification" };
	}

	const auto caPublicKey = getCustomPublicKeyFromCertificate (*caCertificate);
	verifyCertificateCustomSignature (certificate, caPublicKey);
}

void doManualVerificationWhenAutomaticFailed (const X509PtrView & certificate, int depth, const CaPublicKeyValidator & validator)
{
	if (2 == depth)
	{
		return doManualVerificationForCaCertificate (certificate, validator);
	}

	if (1 == depth)
	{
		return doManualVerificationForIntermediateCertificate (certificate, validator);
	}

	throw std::runtime_error{ "doManualVerificationWhenAutomaticFailed: unexpected depth" };
}

void printVerifyCertificateCallbackInfo (const X509PtrView & certificate, int error, int depth)
{
	const auto subject = getCertificateSubject (certificate);
	std::cout << "printVerifyCertificateCallbackInfo: automatic verification "
			  << "failed for certificate = " << subject
			  << ", depth = " << depth
			  << ", error = " << error
			  << "; doing manual verification\n";
}

void attemptCertificateManualVerification (
const X509StoreCtxPtrView & storeContext,
const X509PtrView & certificate,
int error,
int depth)
{
	checkAutomaticVerificationFailureWasExpected (depth, error);

	const auto * ssl = static_cast<const SSL *> (X509_STORE_CTX_get_ex_data (*storeContext, SSL_get_ex_data_X509_STORE_CTX_idx ()));
	if (!ssl)
	{
		throw std::runtime_error{ "attemptCertificateManualVerification: X509_STORE_CTX_get_ex_data: " + getLastOpenSslError () };
	}

	const auto * validator = SSL_get_ex_data (ssl, getSslExDataIndex ());
	if (!validator)
	{
		throw std::runtime_error{ "attemptCertificateManualVerification: SSL_get_ex_data: " + getLastOpenSslError () };
	}

	doManualVerificationWhenAutomaticFailed (certificate, depth, *reinterpret_cast<const CaPublicKeyValidator *> (validator));
}

void verifyCertificateCallbackImpl (int automaticVerificationResult, const X509StoreCtxPtrView & storeContext)
{
	if (1 == automaticVerificationResult)
	{
		std::cout << "verifyCertificateCallbackImpl: automatic verification succeeded => ACCEPT\n";
	}
	else
	{
		const auto depth = X509_STORE_CTX_get_error_depth (*storeContext);
		if (0 > depth)
		{
			throw std::runtime_error{ "verifyCertificateCallbackImpl: X509_STORE_CTX_get_error_depth: " + getLastOpenSslError () };
		}

		const auto error = X509_STORE_CTX_get_error (*storeContext);
		const auto certificate = X509PtrView::make (X509_STORE_CTX_get_current_cert (*storeContext));
		printVerifyCertificateCallbackInfo (certificate, error, depth);

		attemptCertificateManualVerification (storeContext, certificate, error, depth);
		std::cout << "verifyCertificateCallbackImpl: manual verification succeeded => ACCEPT\n";
	}
}

int verifyCertificateCallback (int automaticVerificationResult, X509_STORE_CTX * storeContext)
{
	try
	{
		verifyCertificateCallbackImpl (automaticVerificationResult, X509StoreCtxPtrView::make (storeContext));
		return 1;
	}
	catch (const std::exception & ex)
	{
		std::cout << "verifyCertificateCallback: manual verification failed: " << ex.what () << " => REJECT\n";
	}
	catch (...)
	{
		std::cout << "verifyCertificateCallback: manual verification failed => REJECT\n";
	}

	return 0;
}

void configureSslContext (const SslCtxPtrView & sslContext)
{
	auto mode = SSL_CTX_get_verify_mode (*sslContext);
	if (mode < 0)
	{
		throw std::runtime_error{ "configureSslContext: SSL_CTX_get_verify_mode: " + getLastOpenSslError () };
	}
	//	else if (mode == 0)
	//	{
	////		mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
	//		mode = SSL_VERIFY_NONE;
	//	}

	SSL_CTX_set_verify (*sslContext, mode, verifyCertificateCallback);

	const auto verifyParam = X509VerifyParamPtr::make ();
	if (1 != X509_VERIFY_PARAM_set_flags (*verifyParam, X509_V_FLAG_CHECK_SS_SIGNATURE))
	{
		throw std::runtime_error{ "configureSslContext: X509_VERIFY_PARAM_set_flags: " + getLastOpenSslError () };
	}

	if (1 != SSL_CTX_set1_param (*sslContext, *verifyParam))
	{
		throw std::runtime_error{ "configureSslContext: SSL_CTX_set1_param: " + getLastOpenSslError () };
	}
}

}

boost::filesystem::path operator/ (boost::filesystem::path const & lhs, std::string_view const & rhs)
{
	return boost::filesystem::path{ lhs } / rhs.data ();
}
