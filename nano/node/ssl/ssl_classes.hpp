#pragma once

#include <nano/node/ssl/ssl_ptr.hpp>

#include <boost/asio/ssl/context.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <openssl/x509v3.h>

namespace nano::ssl
{
class BufferView;
using Buffer = std::vector<std::uint8_t>;
using CertificateSignature = std::pair<AlgorithmPtrView, Asn1BitStringPtrView>;
using ExpectedFailuresMap = std::unordered_map<int, std::vector<int>>;
using CaPublicKeyValidator = std::function<const std::optional<X509Ptr> &(const std::optional<BufferView> & publicKey, std::optional<X509Ptr> caCertificate)>;

constexpr std::string_view PKI_RESOURCES_DIRECTORY_PATH = "pki";
constexpr std::string_view CERTIFICATES_CHAIN_PEM_FILE = "chain.pem";
constexpr std::string_view LEAF_PRIVATE_KEY_PEM_FILE = "leaf.prv.pem";

/**
 * In-house std::span since existing code was already working with std::spans
 */
class BufferView
{
public:
	BufferView (const std::uint8_t * data, std::size_t size);

	BufferView (const Buffer & buffer); // NOLINT(google-explicit-constructor)

	[[nodiscard]] const std::uint8_t * getData () const;

	[[nodiscard]] std::size_t getSize () const;

	[[nodiscard]] bool isEmpty () const;

	[[nodiscard]] const std::uint8_t * begin () const;

	[[nodiscard]] const std::uint8_t * end () const;

	[[nodiscard]] const std::uint8_t & operator[] (std::size_t pos) const;

	[[nodiscard]] explicit operator Buffer () const;

	[[nodiscard]] bool operator== (const Buffer & rhs) const;

	[[nodiscard]] bool operator!= (const Buffer & rhs) const;

private:
	const std::uint8_t * mData;
	std::size_t mSize;
};

class AdditionalSignature
{
public:
	AdditionalSignature (Buffer publicKey, Buffer signature);

	[[nodiscard]] Buffer & getPublicKey ();

	[[nodiscard]] const Buffer & getPublicKey () const;

	[[nodiscard]] const Buffer & getSignature () const;

private:
	Buffer mPublicKey;
	Buffer mSignature;
};

class VerifiedCertificateSignatures
{
public:
	explicit VerifiedCertificateSignatures (Buffer verifiedData);

	[[nodiscard]] const Buffer & getDataToBeVerified () const;

	void setPublicKey (Buffer publicKey);

	[[nodiscard]] const std::optional<Buffer> & getPublicKey () const;

	void setAdditionalSignaturesPublicKeys (std::vector<Buffer> additionalSignaturesPublicKeys);

	[[nodiscard]] const std::optional<std::vector<Buffer>> & getAdditionalSignaturesPublicKeys () const;

private:
	Buffer mDataToBeVerified;
	std::optional<Buffer> mPublicKey;
	std::optional<std::vector<Buffer>> mAdditionalSignaturesPublicKeys;
};

class RecursiveCallGuard
{
public:
	explicit RecursiveCallGuard (bool & hasBeenCalled);

	~RecursiveCallGuard ();

private:
	bool & mHasBeenCalled;
};

class CertificateDataToBeSignedCleaner
{
public:
	explicit CertificateDataToBeSignedCleaner (const X509PtrView & certificate);

	~CertificateDataToBeSignedCleaner ();

	[[nodiscard]] const X509ExtensionPtr & getAdditionalSignaturesExtension () const;

private:
	const X509PtrView & mCertificate;
	X509ExtensionPtr mAdditionalSignaturesExtension;
};

class X509V3Ctx
{
public:
	X509V3Ctx (const X509PtrView & issuer, const X509PtrView & subject);

	[[nodiscard]] X509V3_CTX & operator* ();

	[[nodiscard]] const X509V3_CTX & operator* () const;

	[[nodiscard]] X509V3_CTX * breakConst () const;

private:
	X509V3_CTX mContext;
};

class key_group
{
public:
	key_group (std::string_view prv, std::string_view pub) :
		key_private{ std::move (prv) },
		key_public{ std::move (pub) }
	{
	}
	std::string_view const key_private;
	std::string_view const key_public;
};

class ssl_context
{
public:
	ssl_context (key_group const & key_group, std::filesystem::path const & certificate_dir = std::filesystem::path{ PKI_RESOURCES_DIRECTORY_PATH });

	[[nodiscard]] boost::asio::ssl::context & get ();

	[[nodiscard]] boost::asio::ssl::context & operator* ();

private:
	boost::asio::ssl::context m_value;

	void configure ();
};

class ssl_manual_validation_request_handler
{
public:
	using ca_certificate = std::optional<nano::ssl::X509Ptr>;

	ssl_manual_validation_request_handler ();

	[[nodiscard]] const ca_certificate & on_set_request (nano::ssl::X509Ptr ca_certificate);

	[[nodiscard]] const ca_certificate & on_get_request ();

	[[nodiscard]] const ca_certificate & on_validate_request (const nano::ssl::BufferView & public_key);

private:
	ca_certificate m_ca_certificate;
};

class ssl_manual_validation_ensurer
{
public:
	ssl_manual_validation_ensurer ();

	[[nodiscard]] bool was_invoked () const;

	[[nodiscard]] nano::ssl::CaPublicKeyValidator & get_handler ();

private:
	bool m_was_invoked;
	nano::ssl::CaPublicKeyValidator m_invokable;
	ssl_manual_validation_request_handler m_request_handler;

	[[nodiscard]] const ssl_manual_validation_request_handler::ca_certificate & validate (
	const std::optional<nano::ssl::BufferView> & public_key,
	std::optional<nano::ssl::X509Ptr> ca_certificate = std::nullopt);
};

}
