#pragma once

#include <nano/node/ssl/ssl_error.hpp>
#include <nano/node/ssl/ssl_ptr_helper.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/ossl_typ.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

namespace nano::ssl
{
template <typename, auto, auto, auto>
class OpenSslPtrView;

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
class OpenSslPtr
{
public:
	using Self = OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>;

	OpenSslPtr ();

	OpenSslPtr (const Self & rhs);

	OpenSslPtr (OpenSslPtr && rhs) noexcept;

	~OpenSslPtr ();

	Self & operator= (const Self & rhs);

	Self & operator= (Self && rhs) noexcept;

	static Self make ();

	static Self make (DataT * data);

	void reset ();

	[[nodiscard]] DataT * get () const;

	DataT * operator* () const;

	DataT ** getAddress ();

	DataT * release ();

	explicit operator bool () const;

protected:
	explicit OpenSslPtr (DataT * data);

	void markAsView ();

private:
	DataT * mData;
	bool mIsView;

	[[nodiscard]] bool isView () const;

	void increaseReferences ();

	void decreaseReferences ();

	friend class OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>;
};

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
class OpenSslPtrView : public OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>
{
public:
	using Base = OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>;
	using Self = OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>;

	OpenSslPtrView ();

	OpenSslPtrView (const Base & rhs); // NOLINT(google-explicit-constructor)

	OpenSslPtrView (const OpenSslPtrView & rhs);

	OpenSslPtrView (OpenSslPtrView && rhs) noexcept;

	static Self make ();

	static Self make (DataT * data);

	OpenSslPtrView & operator= (OpenSslPtrView && rhs) noexcept;

protected:
	explicit OpenSslPtrView (DataT * data);
};

#define DECLARE_OPENSSL_PTR(name, DataType, createFunction, increaseReferencesFunction, decreaseReferencesFunction) \
	using name##Ptr = OpenSslPtr<DataType, createFunction, increaseReferencesFunction, decreaseReferencesFunction>; \
	using name##PtrView = OpenSslPtrView<DataType, createFunction, increaseReferencesFunction, decreaseReferencesFunction>;

DECLARE_OPENSSL_PTR (Algorithm, X509_ALGOR, X509_ALGOR_new, nullptr, X509_ALGOR_free);
DECLARE_OPENSSL_PTR (Asn1BitString, ASN1_BIT_STRING, ASN1_BIT_STRING_new, nullptr, ASN1_BIT_STRING_free);
DECLARE_OPENSSL_PTR (Asn1Integer, ASN1_INTEGER, ASN1_INTEGER_new, nullptr, ASN1_INTEGER_free);
DECLARE_OPENSSL_PTR (Asn1Object, ASN1_OBJECT, ASN1_OBJECT_new, nullptr, ASN1_OBJECT_free);
DECLARE_OPENSSL_PTR (Asn1OctetString, ASN1_OCTET_STRING, ASN1_OCTET_STRING_new, nullptr, ASN1_OCTET_STRING_free);
DECLARE_OPENSSL_PTR (Asn1Sequence, ASN1_SEQUENCE_ANY, sk_ASN1_TYPE_new_null, nullptr, detail::deleteSequence);
DECLARE_OPENSSL_PTR (Asn1Time, ASN1_TIME, ASN1_TIME_new, nullptr, ASN1_TIME_free);
DECLARE_OPENSSL_PTR (Asn1Type, ASN1_TYPE, ASN1_TYPE_new, nullptr, ASN1_TYPE_free);
DECLARE_OPENSSL_PTR (Bio, BIO, BIO_new, BIO_up_ref, BIO_free);
DECLARE_OPENSSL_PTR (ConstBioMethod, const BIO_METHOD, nullptr, nullptr, detail::getConstNoOpDeleter<BIO_METHOD> ());
DECLARE_OPENSSL_PTR (Buffer, std::uint8_t, nullptr, nullptr, detail::deleteBuffer);
DECLARE_OPENSSL_PTR (ConstBuffer, const std::uint8_t, nullptr, nullptr, detail::deleteBuffer);
DECLARE_OPENSSL_PTR (EvpPkey, EVP_PKEY, EVP_PKEY_new, EVP_PKEY_up_ref, EVP_PKEY_free);
DECLARE_OPENSSL_PTR (EvpPkeyCtx, EVP_PKEY_CTX, EVP_PKEY_CTX_new, nullptr, EVP_PKEY_CTX_free);
DECLARE_OPENSSL_PTR (Ssl, SSL, SSL_new, SSL_up_ref, SSL_free);
DECLARE_OPENSSL_PTR (SslCtx, SSL_CTX, SSL_CTX_new, SSL_CTX_up_ref, SSL_CTX_free);
DECLARE_OPENSSL_PTR (X509, X509, X509_new, X509_up_ref, X509_free);
DECLARE_OPENSSL_PTR (X509Extension, X509_EXTENSION, X509_EXTENSION_new, nullptr, X509_EXTENSION_free);
DECLARE_OPENSSL_PTR (X509Name, X509_NAME, X509_NAME_new, nullptr, X509_NAME_free);
DECLARE_OPENSSL_PTR (X509StoreCtx, X509_STORE_CTX, X509_STORE_CTX_new, nullptr, X509_STORE_CTX_free);
DECLARE_OPENSSL_PTR (X509VerifyParam, X509_VERIFY_PARAM, X509_VERIFY_PARAM_new, nullptr, X509_VERIFY_PARAM_free);

}

namespace nano::ssl
{
template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::OpenSslPtr () :
	mData{},
	mIsView{ false }
{
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::OpenSslPtr (const Self & rhs) :
	mData{ rhs.mData },
	mIsView{ rhs.mIsView }
{
	increaseReferences ();
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::OpenSslPtr (OpenSslPtr && rhs) noexcept
	:
	mData{ rhs.mData },
	mIsView{ rhs.mIsView }
{
	rhs.mData = nullptr;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::~OpenSslPtr ()
{
	reset ();
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
typename OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::Self &
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::operator= (const Self & rhs)
{
	if (this != &rhs)
	{
		reset ();

		mData = rhs.mData;
		mIsView = rhs.mIsView;

		increaseReferences ();
	}

	return *this;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
typename OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::Self &
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::operator= (Self && rhs) noexcept
{
	reset ();

	mData = rhs.mData;
	rhs.mData = nullptr;

	mIsView = rhs.mIsView;

	return *this;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
typename OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::Self
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::make ()
{
	Self result{ createFunction () };
	if (!result)
	{
		throw std::runtime_error{ "OpenSslPtr::make: createFunction: " + nano::ssl::getLastOpenSslError () };
	}

	return result;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
typename OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::Self
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::make (DataT * data)
{
	Self result{ data };
	if (!result)
	{
		throw std::runtime_error{ "OpenSslPtr::make: " + nano::ssl::getLastOpenSslError () };
	}

	return result;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
void OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::reset ()
{
	decreaseReferences ();
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
DataT * OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::get () const
{
	return mData;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
DataT * OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::operator* () const
{
	return get ();
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
DataT ** OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::getAddress ()
{
	return &mData;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
DataT * OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::release ()
{
	const auto * result = get ();
	mData = nullptr;

	return result;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::operator bool () const
{
	return mData;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::OpenSslPtr (DataT * data) :
	mData{ data },
	mIsView{ false }
{
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
void OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::markAsView ()
{
	mIsView = true;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
bool OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::isView () const
{
	return mIsView;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
void OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::increaseReferences ()
{
	if (get () && !isView ())
	{
		if (1 != increaseReferencesFunction (get ()))
		{
			throw std::runtime_error{ "OpenSslPtr::increaseReferences: increaseReferencesFunction: " + nano::ssl::getLastOpenSslError () };
		}
	}
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
void OpenSslPtr<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::decreaseReferences ()
{
	if constexpr (std::is_invocable_v<decltype (decreaseReferencesFunction), DataT *>)
	{
		if (get () && !isView ())
		{
			if constexpr (std::is_invocable_r_v<int, decltype (decreaseReferencesFunction), DataT *>)
			{
				if (1 != decreaseReferencesFunction (get ()))
				{
					std::cout << "\nOpenSslPtr::increaseReferences: "
								 "increaseReferencesFunction: "
					+ nano::ssl::getLastOpenSslError ()
							  << std::endl;
				}
			}
			else
			{
				decreaseReferencesFunction (get ());
			}

			mData = nullptr;
		}
	}
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::OpenSslPtrView () :
	Base{}
{
	Base::markAsView ();
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::OpenSslPtrView (const Base & rhs) :
	Base{ rhs.mData }
{
	Base::markAsView ();
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::OpenSslPtrView (const OpenSslPtrView & rhs) :
	Base{ rhs.mData }
{
	Base::markAsView ();
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::OpenSslPtrView (OpenSslPtrView && rhs) noexcept
	:
	Base{ std::move (rhs) }
{
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
typename OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::Self
OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::make ()
{
	throw std::runtime_error{ "OpenSslPtrView::make: logic error: empty make should not be called within views" };
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
typename OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::Self
OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::make (DataT * data)
{
	auto base = Base::make (data);
	base.markAsView ();

	return base;
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
typename OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::Self &
OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::operator= (OpenSslPtrView && rhs) noexcept
{
	return static_cast<OpenSslPtrView &> (Base::operator= (std::move (rhs))); // NOLINT(misc-unconventional-assign-operator)
}

template <typename DataT, auto createFunction, auto increaseReferencesFunction, auto decreaseReferencesFunction>
OpenSslPtrView<DataT, createFunction, increaseReferencesFunction, decreaseReferencesFunction>::OpenSslPtrView (DataT * data) :
	Base{ data }
{
}

}
