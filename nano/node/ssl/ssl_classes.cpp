#include <nano/node/ssl/ssl_classes.hpp>
#include <nano/node/ssl/ssl_functions.hpp>
#include <nano/node/ssl/ssl_recognize_rep_keys.hpp>

#include <iostream>
#include <mutex>
#include <stdexcept>

namespace
{
const nano::ssl::ssl_manual_validation_request_handler::ca_certificate & on_invalid_manual_validation_request ()
{
	throw std::runtime_error{ "on_invalid_manual_validation_request: invalid request" };
}

}

namespace nano::ssl
{
BufferView::BufferView (const std::uint8_t * data, std::size_t size) :
	mData{ data },
	mSize{ size }
{
}

BufferView::BufferView (const Buffer & buffer) :
	BufferView (buffer.data (), buffer.size ())
{
}

const std::uint8_t * BufferView::getData () const
{
	return mData;
}

std::size_t BufferView::getSize () const
{
	return mSize;
}

bool BufferView::isEmpty () const
{
	return 0 == getSize ();
}

const std::uint8_t * BufferView::begin () const
{
	return getData ();
}

const std::uint8_t * BufferView::end () const
{
	return getData () + getSize ();
}

const std::uint8_t & BufferView::operator[] (std::size_t pos) const
{
	return getData ()[pos];
}

BufferView::operator Buffer () const
{
	return Buffer (begin (), end ()); // NOLINT(modernize-return-braced-init-list)
}

bool BufferView::operator== (const Buffer & rhs) const
{
	return getSize () == rhs.size () && 0 == std::memcmp (getData (), rhs.data (), getSize ());
}

bool BufferView::operator!= (const Buffer & rhs) const
{
	return !operator== (rhs);
}

AdditionalSignature::AdditionalSignature (Buffer publicKey, Buffer signature) :
	mPublicKey{ std::move (publicKey) },
	mSignature{ std::move (signature) }
{
}

Buffer & AdditionalSignature::getPublicKey ()
{
	return mPublicKey;
}

const Buffer & AdditionalSignature::getPublicKey () const
{
	return const_cast<AdditionalSignature *> (this)->getPublicKey ();
}

const Buffer & AdditionalSignature::getSignature () const
{
	return mSignature;
}

VerifiedCertificateSignatures::VerifiedCertificateSignatures (Buffer verifiedData) :
	mDataToBeVerified{ std::move (verifiedData) },
	mPublicKey{},
	mAdditionalSignaturesPublicKeys{}
{
}

const Buffer & VerifiedCertificateSignatures::getDataToBeVerified () const
{
	return mDataToBeVerified;
}

void VerifiedCertificateSignatures::setPublicKey (Buffer publicKey)
{
	if (mPublicKey.has_value ())
	{
		throw std::runtime_error{ "VerifiedCertificateSignatures::setPublicKey: public key already set" };
	}

	mPublicKey = std::move (publicKey);
}

const std::optional<Buffer> & VerifiedCertificateSignatures::getPublicKey () const
{
	return mPublicKey;
}

void VerifiedCertificateSignatures::setAdditionalSignaturesPublicKeys (std::vector<Buffer> additionalSignaturesPublicKeys)
{
	if (mAdditionalSignaturesPublicKeys.has_value ())
	{
		throw std::runtime_error{ "VerifiedCertificateSignatures::setAdditionalSignaturesPublicKeys: "
								  "additional signatures public keys already set" };
	}

	mAdditionalSignaturesPublicKeys = std::move (additionalSignaturesPublicKeys);
}

const std::optional<std::vector<Buffer>> & VerifiedCertificateSignatures::getAdditionalSignaturesPublicKeys () const
{
	return mAdditionalSignaturesPublicKeys;
}

RecursiveCallGuard::RecursiveCallGuard (bool & hasBeenCalled) :
	mHasBeenCalled{ hasBeenCalled }
{
	if (mHasBeenCalled)
	{
		throw std::runtime_error{ "RecursiveCallGuard: logic error" };
	}

	mHasBeenCalled = true;
}

RecursiveCallGuard::~RecursiveCallGuard ()
{
	mHasBeenCalled = false;
}

CertificateDataToBeSignedCleaner::CertificateDataToBeSignedCleaner (const X509PtrView & certificate) :
	mCertificate{ certificate },
	mAdditionalSignaturesExtension{ removeAndGetCertificateAdditionalSignaturesExtension (mCertificate) }
{
}

CertificateDataToBeSignedCleaner::~CertificateDataToBeSignedCleaner ()
{
	if (mAdditionalSignaturesExtension)
	{
		if (1 != X509_add_ext (*mCertificate, *mAdditionalSignaturesExtension, -1))
		{
			std::cout << "\nCertificateDataToBeSignedCleaner::~CertificateDataToBeSignedCleaner: "
						 "X509_add_ext: "
			+ nano::ssl::getLastOpenSslError ()
					  << std::endl;
		}
	}
}

const X509ExtensionPtr & CertificateDataToBeSignedCleaner::getAdditionalSignaturesExtension () const
{
	return mAdditionalSignaturesExtension;
}

X509V3Ctx::X509V3Ctx (const X509PtrView & issuer, const X509PtrView & subject) :
	mContext{}
{
	X509V3_set_ctx (&mContext, *issuer, *subject, nullptr, nullptr, 0);
}

X509V3_CTX & X509V3Ctx::operator* ()
{
	return mContext;
}

const X509V3_CTX & X509V3Ctx::operator* () const
{
	return mContext;
}

X509V3_CTX * X509V3Ctx::breakConst () const
{
	return &const_cast<X509V3Ctx *> (this)->operator* ();
}

ssl_context::ssl_context (const std::filesystem::path & certificate_dir, const key_group_t key_group) :
	m_value{ boost::asio::ssl::context::tlsv12 }
{
	static std::once_flag once_flag{};
	std::call_once (once_flag, [certificate_dir, key_group] () { nano::ssl::generatePki (certificate_dir, key_group); });

	configure ();
}

boost::asio::ssl::context & ssl_context::get ()
{
	return m_value;
}

boost::asio::ssl::context & ssl_context::operator* ()
{
	return get ();
}

void ssl_context::configure ()
{
	const std::filesystem::path pki_resources_directory{ nano::ssl::PKI_RESOURCES_DIRECTORY_PATH };
	const auto certificates_chain_file = pki_resources_directory / nano::ssl::CERTIFICATES_CHAIN_PEM_FILE;
	const auto leaf_private_key_file = pki_resources_directory / nano::ssl::LEAF_PRIVATE_KEY_PEM_FILE;

	m_value.set_options (
	//	boost::asio::ssl::context::no_sslv2 | boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::no_tlsv1 | boost::asio::ssl::context::no_tlsv1_1 | boost::asio::ssl::context::no_tlsv1_2);
	boost::asio::ssl::context::no_sslv2 | boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::no_tlsv1 | boost::asio::ssl::context::no_tlsv1_1);

	m_value.set_verify_mode (boost::asio::ssl::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert);
	m_value.use_certificate_chain_file (certificates_chain_file.native ());
	m_value.use_private_key_file (leaf_private_key_file.native (), boost::asio::ssl::context::pem);

	nano::ssl::configureSslContext (nano::ssl::SslCtxPtrView::make (m_value.native_handle ()));
}

ssl_manual_validation_request_handler::ssl_manual_validation_request_handler () :
	m_ca_certificate{}
{
}

const ssl_manual_validation_request_handler::ca_certificate & ssl_manual_validation_request_handler::on_set_request (nano::ssl::X509Ptr ca_certificate)
{
	if (m_ca_certificate.has_value ())
	{
		return on_invalid_manual_validation_request ();
	}

	m_ca_certificate = std::move (ca_certificate);
	return m_ca_certificate;
}

const ssl_manual_validation_request_handler::ca_certificate & ssl_manual_validation_request_handler::on_get_request ()
{
	return m_ca_certificate;
}

const ssl_manual_validation_request_handler::ca_certificate & ssl_manual_validation_request_handler::on_validate_request (const nano::ssl::BufferView & public_key)
{
	if (!is_ca_public_key_valid (public_key))
	{
		throw std::runtime_error{ "ssl_manual_validation_request_handler: validation error: unknown root CA public key -- this can be a potential MiTM attack" };
	}

	return m_ca_certificate;
}

ssl_manual_validation_ensurer::ssl_manual_validation_ensurer () :
	m_was_invoked{ false },
	m_invokable{},
	m_request_handler{}
{
	m_invokable = [this] (const std::optional<BufferView> & publicKey, std::optional<X509Ptr> caCertificate) -> const std::optional<X509Ptr> & {
		return this->validate (publicKey, caCertificate);
	};
}

bool ssl_manual_validation_ensurer::was_invoked () const
{
	return m_was_invoked;
}

nano::ssl::CaPublicKeyValidator & ssl_manual_validation_ensurer::get_handler ()
{
	return m_invokable;
}

const ssl_manual_validation_request_handler::ca_certificate & ssl_manual_validation_ensurer::validate (
const std::optional<nano::ssl::BufferView> & public_key,
std::optional<nano::ssl::X509Ptr> ca_certificate)
{
	if (public_key.has_value () && ca_certificate.has_value ())
	{
		return on_invalid_manual_validation_request ();
	}

	if (!public_key.has_value () && !ca_certificate.has_value ())
	{
		return m_request_handler.on_get_request ();
	}

	if (public_key.has_value ())
	{
		// TODO: count how many times it was invoked and set an exact number of invokations per handshake

		std::cout << "ssl_manual_validation_ensurer: invoked" << std::endl;
		m_was_invoked = true;

		return m_request_handler.on_validate_request (*public_key);
	}

	if (ca_certificate.has_value ())
	{
		return m_request_handler.on_set_request (std::move (*ca_certificate));
	}

	throw std::runtime_error{ "ssl_manual_validation_ensurer: logic error" };
}

}
