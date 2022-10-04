#pragma once

#include <nano/node/ssl/ssl_classes.hpp>
#include <nano/node/ssl/ssl_ptr.hpp>

#include <cstdint>
#include <filesystem>
#include <ios>
#include <stdexcept>
#include <string>
#include <string_view>

namespace nano::ssl
{
std::string binaryToHex (const BufferView & input);

Buffer hexToBinary (const std::string_view & input);

Buffer getCaPrivateKey (const key_group_t key_group);

Buffer getCaPublicKey (const key_group_t key_group);

std::string readFromBio (const BioPtrView & bio);

int getSslExDataIndex ();

int getAdditionalSignaturesExtensionObjectNid ();

const ExpectedFailuresMap & getAutomaticVerificationExpectedFailures ();

void setCaPublicKeyValidator (const SslPtrView & ssl, CaPublicKeyValidator & validator);

std::ifstream openFileForReading (const std::filesystem::path & filePath, std::ios_base::openmode mode);

std::string readFromFile (const std::filesystem::path & filePath);

EvpPkeyPtr parsePrivateKeyFromPemFile (const std::filesystem::path & filePath);

EvpPkeyPtr parsePublicKeyFromPemFile (const std::filesystem::path & filePath);

X509Ptr parseCertificateFromPemFile (const std::filesystem::path & filePath);

std::ofstream openFileForWriting (const std::filesystem::path & filePath, std::ios_base::openmode mode);

void writeToFile (const std::string_view & data, const std::filesystem::path & filePath);

void serializePrivateKeyIntoPemFile (const EvpPkeyPtrView & privateKey, const std::filesystem::path & filePath);

void serializePublicKeyIntoPemFile (const EvpPkeyPtrView & publicKey, const std::filesystem::path & filePath);

void serializeCertificateIntoPemFile (const X509PtrView & certificate, const std::filesystem::path & filePath);

CertificateSignature getCertificateSignatureImpl (const X509PtrView & certificate);

void setAlgorithmTypeToEd25519 (const AlgorithmPtrView & algorithm);

void addCertificateCustomSignature (const X509PtrView & certificate, const BufferView & signature);

EvpPkeyPtr getEvpPublicKeyFromCustomPublicKey (const BufferView & publicKey);

Buffer getCustomPublicKeyFromEvpPublicKey (const EvpPkeyPtrView & publicKey);

Buffer getCustomPublicKeyFromCertificate (const X509PtrView & certificate);

Asn1OctetStringPtr createAsn1OctetString (const BufferView & data);

Asn1BitStringPtr createAsn1BitString (const BufferView & data);

Asn1TypePtr createAsn1GenericTypeFromObject (const Asn1ObjectPtrView & object);

Asn1TypePtr createAsn1GenericTypeFromBitString (const Asn1BitStringPtrView & bitString);

Asn1TypePtr createAsn1GenericTypeFromSequence (const Asn1OctetStringPtrView & sequence);

Asn1TypePtr createAsn1GenericTypeFromRawBitString (const BufferView & data);

Asn1TypePtr createAsn1GenericTypeFromPublicKey (const BufferView & publicKey);

Asn1TypePtr createAsn1GenericTypeFromSignature (const BufferView & signature);

Asn1TypePtr createAsn1GenericTypeFromEd25519Algorithm ();

Asn1SequencePtr createAsn1SequenceFromGenericTypes (const std::vector<Asn1TypePtrView> & genericTypes);

Buffer serializeCertificateDataToBeSignedIntoAsn1 (const X509PtrView & certificate);

Buffer serializeSequenceIntoAsn1 (const Asn1SequencePtrView & sequence);

Buffer serializeAdditionalSignatureIntoAsn1 (const Asn1TypePtrView & algorithmGenericType, const AdditionalSignature & additionalSignature);

std::vector<Buffer> serializeInnerAdditionalSignaturesIntoAsn1 (const std::vector<AdditionalSignature> & additionalSignatures);

Buffer serializeAdditionalSignaturesIntoAsn1 (const std::vector<AdditionalSignature> & additionalSignatures);

void addCertificateAdditionalSignatures (const X509PtrView & certificate, const std::vector<AdditionalSignature> & additionalSignatures);

X509ExtensionPtr removeAndGetCertificateAdditionalSignaturesExtension (const X509PtrView & certificate);

Buffer getCertificateDataToBeSigned (const X509PtrView & certificate);

void setCertificateSubject (const X509PtrView & certificate, const std::string_view & subject);

void setCertificateIssuer (const X509PtrView & certificate, const std::string_view & issuer);

X509Ptr generateCertificate (
const std::string_view & subject,
const std::string_view & issuer,
std::uint64_t serialNumber,
const std::chrono::seconds & validity,
const EvpPkeyPtrView & publicKey);

void verifyCertificateSigningAlgorithm (const X509PtrView & certificate);

void verifyCertificateNormalSignature (const X509PtrView & certificate, const EvpPkeyPtrView & publicKey);

BufferView getCertificateSignature (const X509PtrView & certificate);

void verifyCustomSignature (const BufferView & dataToBeVerified, const BufferView & publicKey, const BufferView & signature);

void verifyCertificateCustomSignature (
const X509PtrView & certificate,
const BufferView & publicKey,
const std::optional<BufferView> & dataToBeVerified = std::nullopt);

void addIsCaExtension (const X509PtrView & certificate, const X509V3Ctx & extensionContext, bool isCa);

Buffer getCustomPublicKeyFromCustomPrivateKey (const BufferView & privateKey);

Buffer createCustomSignature (const BufferView & privateKey, const BufferView & dataToBeSigned);

EvpPkeyPtr generatePrivateKey ();

EvpPkeyPtr generatePrivateKeyAndSave (const std::filesystem::path & privatePemFile, const std::filesystem::path & publicPemFile);

void addCertificateFakeSignature (const X509PtrView & certificate);

std::vector<AdditionalSignature> createDummyAdditionalSignatures (
const BufferView & dataToBeSigned,
const BufferView & privateKey,
const BufferView & publicKey);

void signCaCertificate (const X509PtrView & certificate, const BufferView & privateKey, const BufferView & publicKey);

Asn1SequencePtr parseSequenceFromAsn1 (const BufferView & data);

std::vector<AdditionalSignature> getCertificateAdditionalSignatures (const X509ExtensionPtrView & additionalSignaturesExtension);

std::optional<std::vector<Buffer>> getPublicKeysFromAdditionalSignatures (std::vector<AdditionalSignature> & additionalSignatures);

VerifiedCertificateSignatures verifyCertificateAdditionalSignatures (const X509PtrView & certificate);

VerifiedCertificateSignatures verifyCaCertificate (const X509PtrView & certificate);

void markCertificateAsCa (const X509PtrView & certificate, const X509V3Ctx & extensionContext);

void generateCaCertificate (const std::filesystem::path & resources_dir, const key_group_t key_group);

void signIntermediateCertificate (const X509PtrView & certificate, const BufferView & privateKey);

void generateIntermediateCertificate (const std::filesystem::path & resources_dir, const key_group_t key_group);

void signLeafCertificate (const X509PtrView & certificate, const EvpPkeyPtrView & privateKey);

void markCertificateAsLeaf (const X509PtrView & certificate, const X509V3Ctx & extensionContext);

void generateLeafCertificate (const std::filesystem::path & resources_dir);

void composeCertificateChainPemFile (const std::filesystem::path & resources_dir);

void createResourcesDirectory (const std::filesystem::path & resources_dir);

void generatePki (const std::filesystem::path &, const key_group_t);

std::string getCertificateSubject (const X509PtrView & certificate);

void checkAutomaticVerificationFailureWasExpected (int depth, int error);

void validateVerifiedCaCertificateSignatures (const VerifiedCertificateSignatures & verifiedCaCertificateSignatures, const CaPublicKeyValidator & validator);

void doManualVerificationForCaCertificate (const X509PtrView & certificate, const CaPublicKeyValidator & validator);

void doManualVerificationForIntermediateCertificate (const X509PtrView & certificate, const CaPublicKeyValidator & validator);

void doManualVerificationWhenAutomaticFailed (const X509PtrView & certificate, int depth, const CaPublicKeyValidator & validator);

void printVerifyCertificateCallbackInfo (const X509PtrView & certificate, int error, int depth);

void attemptCertificateManualVerification (
const X509StoreCtxPtrView & storeContext,
const X509PtrView & certificate,
int error,
int depth);

void verifyCertificateCallbackImpl (int automaticVerificationResult, const X509StoreCtxPtrView & storeContext);

int verifyCertificateCallback (int automaticVerificationResult, X509_STORE_CTX * storeContext);

void configureSslContext (const SslCtxPtrView & sslContext);

}

namespace nano::ssl
{
template <auto parseFunction, typename OpenSslPtrT>
auto parseFromPem (const std::string_view & data);

template <auto parseFunction, typename OpenSslPtrT>
auto parseFromPemFile (const std::filesystem::path & filePath);

template <typename OpenSslPtrT, typename WriteFunctionT>
std::string serializeIntoPem (const OpenSslPtrT & data, const WriteFunctionT & writeFunction);

template <typename OpenSslPtrT, typename WriteFunctionT>
void serializeIntoPemFile (const OpenSslPtrT & data, const WriteFunctionT & writeFunction, const std::filesystem::path & filePath);

template <auto setFunction, typename OpenSslPtrT>
OpenSslPtrT createAsn1SpecificType (const BufferView & data);

template <typename DataT>
Asn1TypePtr createAsn1GenericType (const DataT * data, std::int32_t type);

template <auto serializationFunction, typename OpenSslPtrT>
Buffer serializeIntoAsn1 (const OpenSslPtrT & data);

template <auto nameGetterFunction>
void setCertificateName (const X509PtrView & certificate, const std::string_view & name);

template <auto parsingFunction, typename OpenSslPtrT>
OpenSslPtrT parseFromAsn1 (const BufferView & data);

}

namespace nano::ssl
{
template <auto parseFunction, typename OpenSslPtrT>
auto parseFromPem (const std::string_view & data)
{
	const auto bio = BioPtr::make (BIO_new_mem_buf (data.data (), static_cast<int> (data.length ())));
	return OpenSslPtrT::make (parseFunction (*bio, nullptr, nullptr, nullptr));
}

template <auto parseFunction, typename OpenSslPtrT>
auto parseFromPemFile (const std::filesystem::path & filePath)
{
	const auto serializedData = readFromFile (filePath);
	return parseFromPem<parseFunction, OpenSslPtrT> (serializedData);
}

template <typename OpenSslPtrT, typename WriteFunctionT>
std::string serializeIntoPem (const OpenSslPtrT & data, const WriteFunctionT & writeFunction)
{
	const auto bioMethod = ConstBioMethodPtr::make (BIO_s_mem ());
	const auto bio = BioPtr::make (BIO_new (*bioMethod));

	if (1 != writeFunction (*bio, *data))
	{
		throw std::runtime_error{ "serializeIntoPem: writeFunction: " + getLastOpenSslError () };
	}

	return readFromBio (bio);
}

template <typename OpenSslPtrT, typename WriteFunctionT>
void serializeIntoPemFile (const OpenSslPtrT & data, const WriteFunctionT & writeFunction, const std::filesystem::path & filePath)
{
	const auto serializedData = serializeIntoPem (data, writeFunction);
	writeToFile (serializedData, filePath);
}

template <auto setFunction, typename OpenSslPtrT>
OpenSslPtrT createAsn1SpecificType (const BufferView & data)
{
	auto result = OpenSslPtrT::make ();
	if (1 != setFunction (*result, const_cast<std::uint8_t *> (data.getData ()), static_cast<int> (data.getSize ())))
	{
		throw std::runtime_error{ "createAsn1SpecificType: setFunction: " + getLastOpenSslError () };
	}

	return result;
}

template <typename DataT>
Asn1TypePtr createAsn1GenericType (const DataT * data, std::int32_t type)
{
	auto result = Asn1TypePtr::make ();
	if (1 != ASN1_TYPE_set1 (*result, type, data))
	{
		throw std::runtime_error{ "createAsn1GenericType: ASN1_TYPE_set1: " + getLastOpenSslError () };
	}

	return result;
}

template <auto serializationFunction, typename OpenSslPtrT>
Buffer serializeIntoAsn1 (const OpenSslPtrT & data)
{
	BufferPtr buffer{};
	const auto bufferSize = serializationFunction (*data, buffer.getAddress ());
	if (1 > bufferSize)
	{
		throw std::runtime_error{ "serializeIntoAsn1: serializationFunction: " + getLastOpenSslError () };
	}

	return Buffer (*buffer, *buffer + bufferSize);
}

template <auto nameGetterFunction>
void setCertificateName (const X509PtrView & certificate, const std::string_view & name)
{
	const auto x509Name = X509NamePtrView::make (nameGetterFunction (*certificate));
	if (1 != X509_NAME_add_entry_by_NID (*x509Name, NID_commonName, MBSTRING_ASC, reinterpret_cast<const std::uint8_t *> (name.data ()), -1, -1, 0))
	{
		throw std::runtime_error{ "setCertificateName: X509_NAME_add_entry_by_NID: " + getLastOpenSslError () };
	}
}

template <auto parsingFunction, typename OpenSslPtrT>
OpenSslPtrT parseFromAsn1 (const BufferView & data)
{
	const auto * dataPtr = data.getData ();
	return OpenSslPtrT::make (parsingFunction (nullptr, &dataPtr, data.getSize ()));
}

}
