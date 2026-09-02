/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include <glog/logging.h>

#include <folly/Function.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/String.h>
#include <folly/container/Access.h>
#include <folly/io/async/ssl/OpenSSLUtils.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

/// Facebook Folly library namespace.
namespace folly {

/// Handler for OpenSSL session tickets.
class OpenSSLTicketHandler;
/// SSL and TLS utilities.
namespace ssl {
/// Collects passwords used to decrypt private keys.
class PasswordCollector;
}

/**
 * Run SSL_accept via a runner
 */
class SSLAcceptRunner {
 public:
  /// Destroys the runner.
  virtual ~SSLAcceptRunner() = default;

  /**
   * This is expected to run the first function and provide its return
   * value to the second function. This can be used to run the SSL_accept
   * in different contexts.
   *
   * @param acceptFunc Function that performs the accept and returns its result.
   * @param finallyFunc Function invoked with the result of \p acceptFunc.
   */
  virtual void run(
      Function<int()> acceptFunc, Function<void(int)> finallyFunc) const {
    finallyFunc(acceptFunc());
  }
};

/**
 * Wrap OpenSSL SSL_CTX into a class.
 */
class SSLContext {
 public:
  /// Callbacks invoked over the lifecycle of an SSL session.
  struct SessionLifecycleCallbacks {
    /// Called when a new session is established.
    /// \param ssl The SSL connection the session belongs to.
    /// \param session The newly established session.
    virtual void onNewSession(
        SSL* ssl, folly::ssl::SSLSessionUniquePtr session) = 0;
    /// Destroys the callbacks object.
    virtual ~SessionLifecycleCallbacks() = default;
  };

  /// Lowest SSL/TLS protocol version to support.
  enum SSLVersion {
    SSLv2, ///< SSL 2.0.
    SSLv3, ///< SSL 3.0.
    TLSv1, ///< support TLS 1.0+
    TLSv1_2, ///< support for only TLS 1.2+
    TLSv1_3, ///< support for only TLS 1.3+
  };

  /**
   * Defines the way that peers are verified.
   * TODO: remove this in favor of the specific client and server enums
   **/
  enum SSLVerifyPeerEnum {
    /// Used by AsyncSSLSocket to delegate to the SSLContext's setting.
    USE_CTX,
    /// Server: request and verify a client cert if sent, do not fail if absent;
    /// client: validate the server certificate or fail.
    VERIFY,
    /// Server: like VERIFY but fail if no certificate is sent;
    /// client: same as VERIFY.
    VERIFY_REQ_CLIENT_CERT,
    /// No verification is done for both server and client side.
    NO_VERIFY
  };

  /// Client-certificate verification policy for a server context.
  enum class VerifyClientCertificate {
    /// Request a cert and verify it; fail if verification fails or none sent.
    ALWAYS,
    /// Request a cert and verify if presented; fail on verification failure,
    /// but do not fail if none is presented.
    IF_PRESENTED,
    /// No verification is done and no cert is requested.
    DO_NOT_REQUEST
  };

  /// Server-certificate verification policy for a client context.
  enum class VerifyServerCertificate {
    /// Server cert is verified and a failure terminates the connection.
    IF_PRESENTED,
    /// Server cert is verified but the result is ignored.
    IGNORE_VERIFY_RESULT
  };

  /// A weighted list of next-protocol names for protocol negotiation.
  struct NextProtocolsItem {
    /// Constructs the item from a weight and its protocol list.
    /// \param wt Relative selection weight.
    /// \param ptcls Protocol names for this weight.
    NextProtocolsItem(int wt, const std::list<std::string>& ptcls)
        : weight(wt), protocols(ptcls) {}
    int weight; ///< Relative selection weight.
    std::list<std::string> protocols; ///< Protocol names for this weight.
  };

  /// Function that selects a client protocol given the server's list.
  using ClientProtocolFilterCallback = bool (*)(
      unsigned char**, unsigned int*, const unsigned char*, unsigned int);

  /**
   * Convenience function to call getErrors() with the current errno value.
   *
   * Make sure that you only call this when there was no intervening operation
   * since the last OpenSSL error that may have changed the current errno value.
   *
   * @return A formatted description of the pending OpenSSL errors.
   */
  static std::string getErrors() { return getErrors(errno); }

  /**
   * Constructor.
   *
   * @param version The lowest or oldest SSL version to support.
   */
  explicit SSLContext(SSLVersion version = TLSv1_2);
  /**
   * Constructor that helps ease migrations by directly wrapping a provided
   * SSL_CTX*
   *
   * @param ctx Existing SSL_CTX to take ownership of.
   */
  explicit SSLContext(SSL_CTX* ctx);
  /// Destroys the context and releases the underlying SSL_CTX.
  virtual ~SSLContext();

  /**
   * Set default TLS 1.2 and below ciphers to be used in SSL handshake process.
   *
   * @param ciphers A list of ciphers to use for TLSv1.0
   */
  virtual void ciphers(const std::string& ciphers);

  /**
   * Low-level method that attempts to set the provided TLS 1.2
   * and below ciphers on the SSL_CTX object,
   * and throws if something goes wrong.
   *
   * @param ciphers Colon-separated OpenSSL cipher list.
   */
  virtual void setCiphersOrThrow(const std::string& ciphers);

  /**
   * Set default TLS 1.2 and below ciphers to be used in SSL handshake process.
   *
   * @param ibegin Iterator to the first cipher name.
   * @param iend Iterator past the last cipher name.
   */
  template <typename Iterator>
  void setCipherList(Iterator ibegin, Iterator iend) {
    if (ibegin != iend) {
      std::string opensslCipherList;
      folly::join(":", ibegin, iend, opensslCipherList);
      setCiphersOrThrow(opensslCipherList);
    }
  }

  /// Sets the cipher list from a container of cipher names.
  /// @param cipherList Container of cipher names.
  template <typename Container>
  void setCipherList(const Container& cipherList) {
    setCipherList(
        folly::access::begin(cipherList), folly::access::end(cipherList));
  }

  /// Sets the cipher list from an initializer list of cipher names.
  /// @param cipherList Cipher names to set.
  template <typename Value>
  void setCipherList(const std::initializer_list<Value>& cipherList) {
    setCipherList(cipherList.begin(), cipherList.end());
  }

  /**
   * Low-level method that attempts to set the provided signature
   * algorithms on the SSL_CTX object for TLS1.2+,
   * and throws if something goes wrong.
   *
   * @param sigAlgs Colon-separated OpenSSL signature-algorithm list.
   */
  virtual void setSigAlgsOrThrow(const std::string& sigAlgs);

  /// Sets the signature algorithms from an iterator range.
  /// @param ibegin Iterator to the first signature-algorithm name.
  /// @param iend Iterator past the last signature-algorithm name.
  template <typename Iterator>
  void setSignatureAlgorithms(Iterator ibegin, Iterator iend) {
    if (ibegin != iend) {
      std::string opensslSigAlgsList;
      join(":", ibegin, iend, opensslSigAlgsList);
      setSigAlgsOrThrow(opensslSigAlgsList);
    }
  }

  /// Sets the signature algorithms from a container of names.
  /// @param sigalgs Container of signature-algorithm names.
  template <typename Container>
  void setSignatureAlgorithms(const Container& sigalgs) {
    setSignatureAlgorithms(
        folly::access::begin(sigalgs), folly::access::end(sigalgs));
  }

  /// Sets the signature algorithms from an initializer list of names.
  /// @param sigalgs Signature-algorithm names to set.
  template <typename Value>
  void setSignatureAlgorithms(const std::initializer_list<Value>& sigalgs) {
    setSignatureAlgorithms(sigalgs.begin(), sigalgs.end());
  }

  /**
   * Sets the list of EC curves supported by the client.
   *
   * @param ecCurves A list of ec curves, eg: P-256
   */
  void setClientECCurvesList(const std::vector<std::string>& ecCurves);

  /// Sets the list of key-exchange groups supported by the context.
  /// @param groups Named groups (e.g. X25519, P-256) to advertise.
  void setSupportedGroups(const std::vector<std::string>& groups);

  /**
   * Method to add support for a specific elliptic curve encryption algorithm.
   *
   * @param curveName: The name of the ec curve to support, eg: prime256v1.
   */
  void setServerECCurve(const std::string& curveName);

  /**
   * Sets an x509 verification param on the context.
   *
   * @param x509VerifyParam Verification parameters to apply.
   */
  void setX509VerifyParam(const ssl::X509VerifyParam& x509VerifyParam);

  /**
   * Method to set verification option in the context object.
   *
   * @param verifyPeer SSLVerifyPeerEnum indicating the verification
   *                       method to use.
   */
  virtual void setVerificationOption(const SSLVerifyPeerEnum& verifyPeer);
  /**
   * Method to set verification options for client and server separately.
   * This is highly recommended as these options are much clearer and the other
   * way will be going away soon
   *
   * @param verifyClient Client-certificate verification policy.
   */
  virtual void setVerificationOption(
      const VerifyClientCertificate& verifyClient);
  /// Sets the server-certificate verification policy for a client context.
  /// @param verifyServer Server-certificate verification policy.
  virtual void setVerificationOption(
      const VerifyServerCertificate& verifyServer);

  /**
   * Method to check if peer verfication is set.
   *
   * @return true if peer verification is required.
   *
   */
  virtual bool needsPeerVerification() const {
    /* TODO this is ugly and i can't think of a reason this should exist
     * will think of what i want to do with this later
     */
    return getVerificationMode() != SSL_VERIFY_NONE;
  }

  /**
   * Method to fetch Verification mode for a SSLVerifyPeerEnum.
   * verifyPeer cannot be SSLVerifyPeerEnum::USE_CTX since there is no
   * context.
   *
   * @param verifyPeer SSLVerifyPeerEnum for which the flags need to
   *                  to be returned
   *
   * @return mode flags that can be used with SSL_set_verify
   */
  static int getVerificationMode(const SSLVerifyPeerEnum& verifyPeer);
  /// Returns the SSL_set_verify mode flags for a client-cert policy.
  /// @param verifyClient Client-certificate verification policy.
  /// @return Mode flags that can be used with SSL_set_verify.
  static int getVerificationMode(const VerifyClientCertificate& verifyClient);
  /// Returns the SSL_set_verify mode flags for a server-cert policy.
  /// @param verifyServer Server-certificate verification policy.
  /// @return Mode flags that can be used with SSL_set_verify.
  static int getVerificationMode(const VerifyServerCertificate& verifyServer);

  /**
   * Method to fetch Verification mode determined by the options
   * set using setVerificationOption.
   *
   * @return mode flags that can be used with SSL_set_verify
   */
  virtual int getVerificationMode() const;

  /**
   * Enable/Disable authentication. Peer name validation can only be done
   * if checkPeerCert is true.
   *
   * @param checkPeerCert If true, require peer to present valid certificate
   * @param checkPeerName If true, validate that the certificate common name
   *                      or alternate name(s) of peer matches the hostname
   *                      used to connect.
   * @param peerName      If non-empty, validate that the certificate common
   *                      name of peer matches the given string (altername
   *                      name(s) are not used in this case).
   */
  virtual void authenticate(
      bool checkPeerCert,
      bool checkPeerName,
      const std::string& peerName = std::string());
  /**
   * Loads a certificate chain stored on disk to be sent to the peer during
   * TLS connection establishment.
   *
   * @param path   Path to the certificate file
   * @param format Certificate file format
   */
  virtual void loadCertificate(const char* path, const char* format = "PEM");
  /**
   * Loads a PEM formatted certificate chain from memory to be sent to the peer
   * during TLS connection establishment.
   *
   * @param cert  A PEM formatted certificate
   */
  virtual void loadCertificateFromBufferPEM(folly::StringPiece cert);

  /**
   * Load private key.
   *
   * @param path   Path to the private key file
   * @param format Private key file format
   */
  virtual void loadPrivateKey(const char* path, const char* format = "PEM");
  /**
   * Load private key from memory.
   *
   * @param pkey  A PEM formatted key
   */
  virtual void loadPrivateKeyFromBufferPEM(folly::StringPiece pkey);

  /**
   * Load cert and key from PEM buffers. Guaranteed to throw if cert and
   * private key mismatch so no need to call isCertKeyPairValid.
   *
   * @param cert A PEM formatted certificate
   * @param pkey A PEM formatted key
   */
  virtual void loadCertKeyPairFromBufferPEM(
      folly::StringPiece cert, folly::StringPiece pkey);

  /**
   * Sets cert chain and key. Guaranteed to throw if cert and private key
   * mismatch.
   *
   * @param certChain A vector of X509 certificates.
   * @param key A private key.
   */
  virtual void setCertChainKeyPair(
      std::vector<ssl::X509UniquePtr>&& certChain, ssl::EvpPkeyUniquePtr&& key);

  /**
   * Load cert and key from files. Guaranteed to throw if cert and key mismatch.
   * Equivalent to calling loadCertificate() and loadPrivateKey().
   *
   * @param certPath   Path to the certificate file
   * @param keyPath   Path to the private key file
   * @param certFormat Certificate file format
   * @param keyFormat Private key file format
   */
  virtual void loadCertKeyPairFromFiles(
      const char* certPath,
      const char* keyPath,
      const char* certFormat = "PEM",
      const char* keyFormat = "PEM");

  /**
   * Call after both cert and key are loaded to check if cert matches key.
   * Must call if private key is loaded before loading the cert.
   * No need to call if cert is loaded first before private key.
   * @return true if matches, or false if mismatch.
   */
  virtual bool isCertKeyPairValid() const;

  /**
   * Load trusted certificates from specified file.
   *
   * @param path Path to trusted certificate file
   */
  virtual void loadTrustedCertificates(const char* path);
  /**
   * Load trusted certificates from a vector of file paths
   *
   * @param paths container of file paths to trusted certificate files
   */
  template <typename StringList>
  void loadTrustedCertificates(const StringList& paths) {
    for (const auto& path : paths) {
      loadTrustedCertificates(path.c_str());
    }
  }
  /**
   * Load trusted certificates from specified X509 certificate store.
   *
   * @param store X509 certificate store.
   */
  virtual void loadTrustedCertificates(X509_STORE* store);

  /**
   * setSupportedClientCertificateAuthorityNames sets the list of acceptable
   * client certificate authoritites that will be sent to the client.
   *
   * This corresponds to the `certificate_authorities` extension.
   *
   * This function does *not* alter the way client authentication is performed
   * in any discernible manner.
   *
   * Certain TLS client implementations will use this list of names to aid in
   * the client certificate selection process.
   *
   * folly::AsyncSSLSocket, which is based on OpenSSL, in particular will
   * *not* use this information. folly::AsyncSSLSocket will send client
   * certificates to whatever `SSLContext::loadCertificate` points to,
   * regardless of what the server sends in `certificate_authorities`.
   *
   * @param names  A vector of X509_NAMEs to send. This typically corresponds
   *               to the Subject of each client certificate authority used
   *               in the trust store.
   *               `OpenSSLUtil::loadNamesFromFile`.
   * @throws std::exception
   */
  void setSupportedClientCertificateAuthorityNames(
      std::vector<ssl::X509NameUniquePtr> names);

  /**
   * setSupportedClientCertificateAuthorityNamesFromFile sets the list of
   * acceptable client certificate authorities that will be sent to the client.
   *
   * See `SSLContext::setSupportedClientCertificateAuthorityNames`.
   *
   * @param path   Path to a file containing PEM encoded X509 certificates.
   * @throws std::exception
   */
  void setSupportedClientCertificateAuthorityNamesFromFile(const char* path) {
    return setSupportedClientCertificateAuthorityNames(
        ssl::OpenSSLUtils::subjectNamesInPEMFile(path));
  }

  /**
   * Override default OpenSSL password collector.
   *
   * @param collector Instance of user defined password collector
   */
  virtual void passwordCollector(
      std::shared_ptr<ssl::PasswordCollector> collector);
  /**
   * Obtain password collector.
   *
   * @return User defined password collector
   */
  virtual std::shared_ptr<ssl::PasswordCollector> passwordCollector() {
    return collector_;
  }

  /**
   * Provide SNI support
   */
  enum ServerNameCallbackResult {
    SERVER_NAME_FOUND, ///< Server name recognized; send it in the Server Hello.
    SERVER_NAME_NOT_FOUND, ///< Name not recognized; continue the handshake.
    SERVER_NAME_NOT_FOUND_ALERT_FATAL, ///< Name not recognized; send a fatal alert.
  };
  /**
   * Callback function from openssl to give the application a
   * chance to check the tlsext_hostname just right after parsing
   * the Client Hello or Server Hello message.
   *
   * It is for the server to switch the SSL to another SSL_CTX
   * to continue the handshake. (i.e. Server Name Indication, SNI, in RFC6066).
   *
   * If the ServerNameCallback returns:
   * SERVER_NAME_FOUND:
   *    server: Send a tlsext_hostname in the Server Hello
   *    client: No-effect
   * SERVER_NAME_NOT_FOUND:
   *    server: Does not send a tlsext_hostname in Server Hello
   *            and continue the handshake.
   *    client: No-effect
   * SERVER_NAME_NOT_FOUND_ALERT_FATAL:
   *    server and client: Send fatal TLS1_AD_UNRECOGNIZED_NAME alert to
   *                       the peer.
   *
   * Quote from RFC 6066:
   * "...
   * If the server understood the ClientHello extension but
   * does not recognize the server name, the server SHOULD take one of two
   * actions: either abort the handshake by sending a fatal-level
   * unrecognized_name(112) alert or continue the handshake.  It is NOT
   * RECOMMENDED to send a warning-level unrecognized_name(112) alert,
   * because the client's behavior in response to warning-level alerts is
   * unpredictable.
   * ..."
   */

  /**
   * Set the ServerNameCallback
   */
  using ServerNameCallback = std::function<ServerNameCallbackResult(SSL* ssl)>;
  /// Sets the callback invoked to handle the SNI server name.
  /// @param cb Callback to install.
  virtual void setServerNameCallback(const ServerNameCallback& cb);

  /**
   * Generic callbacks that are run after we get the Client Hello (right
   * before we run the ServerNameCallback)
   */
  using ClientHelloCallback = std::function<void(SSL* ssl)>;
  /// Adds a callback run after the Client Hello is received.
  /// @param cb Callback to add.
  virtual void addClientHelloCallback(const ClientHelloCallback& cb);

  /**
   * Create an SSL object from this context.
   *
   * @return A new SSL object owned by the caller.
   */
  SSL* createSSL() const;

  /**
   * Sets the namespace to use for sessions created from this context.
   *
   * @param context Session cache context identifier.
   */
  void setSessionCacheContext(const std::string& context);

  /**
   * Set the options on the SSL_CTX object.
   *
   * @param options OpenSSL option flags to set.
   */
  void setOptions(long options);

  /// Returns the ALPN/NPN protocols advertised by this context.
  /// \returns The advertised protocol names in wire format.
  std::string getAdvertisedNextProtocols() const;

  /**
   * Set the list of protocols that this SSL context supports. In client
   * mode, this is the list of protocols that will be advertised for Application
   * Layer Protocol Negotiation (ALPN). In server mode, the first protocol
   * advertised by the client that is also on this list is chosen.
   * Invoking this function with a list of length zero causes ALPN to be
   * disabled.
   *
   * @param protocols   List of protocol names. This method makes a copy,
   *                    so the caller needn't keep the list in scope after
   *                    the call completes. The list must have at least
   *                    one element to enable ALPN. Each element must have
   *                    a string length < 256.
   * @return true if ALPN has been activated. False if ALPN is disabled.
   */
  bool setAdvertisedNextProtocols(const std::list<std::string>& protocols);
  /**
   * Set weighted list of lists of protocols that this SSL context supports.
   * In server mode, each element of the list contains a list of protocols that
   * could be advertised for Application Layer Protocol Negotiation (ALPN).
   * The list of protocols that will be advertised to a client is selected
   * randomly, based on weights of elements. Client mode doesn't support
   * randomized ALPN, so this list should contain only 1 element. The first
   * protocol advertised by the client that is also on the list of protocols
   * of this element is chosen. Invoking this function with a list of length
   * zero causes ALPN to be disabled.
   *
   * @param items  List of NextProtocolsItems, Each item contains a list of
   *               protocol names and weight. After the call of this fucntion
   *               each non-empty list of protocols will be advertised with
   *               probability weight/sum_of_weights. This method makes a copy,
   *               so the caller needn't keep the list in scope after the call
   *               completes. The list must have at least one element with
   *               non-zero weight and non-empty protocols list to enable NPN.
   *               Each name of the protocol must have a string length < 256.
   * @return true if ALPN has been activated. False if ALPN is disabled.
   */
  bool setRandomizedAdvertisedNextProtocols(
      const std::list<NextProtocolsItem>& items);

  /**
   * Disables ALPN on this SSL context.
   */
  void unsetNextProtocols();
  /// Frees the internal buffers holding the advertised protocol strings.
  void deleteNextProtocolsStrings();

  /// Returns whether an ALPN protocol mismatch is tolerated.
  /// \returns True if a mismatch is allowed instead of failing.
  bool getAlpnAllowMismatch() const { return alpnAllowMismatch_; }

  /// Sets whether an ALPN protocol mismatch is tolerated.
  /// @param allowMismatch True to allow a mismatch instead of failing.
  void setAlpnAllowMismatch(bool allowMismatch) {
    alpnAllowMismatch_ = allowMismatch;
  }

  /**
   * Gets the underlying SSL_CTX for advanced usage
   *
   * @return The underlying OpenSSL SSL_CTX.
   */
  SSL_CTX* getSSLCtx() const { return ctx_; }

  /**
   * Examine OpenSSL's error stack, and return a string description of the
   * errors.
   *
   * This operation removes the errors from OpenSSL's error stack.
   *
   * @param errnoCopy Captured errno value to include in the description.
   * @return A formatted description of the pending OpenSSL errors.
   */
  static std::string getErrors(int errnoCopy);

  /// Returns whether peer-name checking is enabled.
  /// \returns True if peer-name checking is enabled.
  bool checkPeerName() const { return checkPeerName_; }
  /// Returns the fixed peer name that connections are validated against.
  /// \returns The expected peer name.
  std::string peerFixedName() const { return peerFixedName_; }

#if defined(SSL_MODE_HANDSHAKE_CUTTHROUGH)
  /**
   * Enable TLS false start, saving a roundtrip for full handshakes. Will only
   * be used if the server uses NPN or ALPN, and a strong forward-secure cipher
   * is negotiated.
   */
  void enableFalseStart();
#endif

  /**
   * Sets the runner used for SSL_accept. If none is given, the accept will be
   * done directly.
   *
   * @param runner Runner to use; ignored if null.
   */
  void sslAcceptRunner(std::unique_ptr<SSLAcceptRunner> runner) {
    if (nullptr == runner) {
      LOG(ERROR) << "Ignore invalid runner";
      return;
    }
    sslAcceptRunner_ = std::move(runner);
  }

  /// Returns the runner currently used for SSL_accept.
  /// \returns The current accept runner.
  const SSLAcceptRunner* sslAcceptRunner() const {
    return sslAcceptRunner_.get();
  }

  /// Installs the handler used for OpenSSL session tickets.
  /// @param handler Ticket handler to take ownership of.
  void setTicketHandler(std::unique_ptr<OpenSSLTicketHandler> handler);

  /// Returns the current OpenSSL session-ticket handler.
  /// \returns The current ticket handler, or null if none is set.
  OpenSSLTicketHandler* getTicketHandler() const {
    return ticketHandler_.get();
  }

  /**
   * Helper to match a hostname versus a pattern.
   *
   * @param host Hostname to check.
   * @param pattern Pattern, possibly containing a wildcard, to match against.
   * @param size Length of \p pattern.
   * @return True if \p host matches \p pattern.
   */
  static bool matchName(const char* host, const char* pattern, int size);

  /**
   * Disable TLS 1.3 in OpenSSL versions that support it.
   */
  void disableTLS13();

  /**
   * Get SSLContext from the ex data of a SSL_CTX.
   *
   * @param ctx SSL_CTX to read the associated SSLContext from.
   * @return The associated SSLContext, or null if none is set.
   */
  static SSLContext* getFromSSLCtx(const SSL_CTX* ctx);

  /// Installs callbacks invoked over the SSL session lifecycle.
  /// @param cb Lifecycle callbacks to take ownership of.
  void setSessionLifecycleCallbacks(
      std::unique_ptr<SessionLifecycleCallbacks> cb);

  /**
   * Set the TLS 1.3 ciphersuites to be used in the SSL handshake, in
   * order of preference.
   * Throws if unsuccessful.
   *
   * @param ciphersuites Colon-separated TLS 1.3 ciphersuite list.
   */
  void setCiphersuitesOrThrow(const std::string& ciphersuites);

  /**
   * Enables/disables non-DHE (Ephemeral Diffie-Hellman) PSK key
   * exchange for TLS 1.3 resumption. Note that this key exchange
   * mode gives up forward secrecy on the resumed session.
   *
   * @param flag True to allow non-DHE PSK key exchange.
   */
  void setAllowNoDheKex(bool flag);

 protected:
  SSL_CTX* ctx_; ///< Underlying OpenSSL SSL_CTX.

 private:
  // TODO deprecate this, it's confusing and the default is bad
  SSLVerifyPeerEnum verifyPeer_{SSLVerifyPeerEnum::NO_VERIFY};

  /* Set one of these values depending on whether you will use the context
   * for a server or client.*/
  VerifyClientCertificate verifyClient_{
      VerifyClientCertificate::DO_NOT_REQUEST};
  VerifyServerCertificate verifyServer_{
      VerifyServerCertificate::IGNORE_VERIFY_RESULT};

  bool checkPeerName_;
  std::string peerFixedName_;
  std::shared_ptr<ssl::PasswordCollector> collector_;
  ServerNameCallback serverNameCb_;
  std::vector<ClientHelloCallback> clientHelloCbs_;

  ClientProtocolFilterCallback clientProtoFilter_{nullptr};

  static bool initialized_;

  std::unique_ptr<SSLAcceptRunner> sslAcceptRunner_;
  std::unique_ptr<OpenSSLTicketHandler> ticketHandler_;

  struct AdvertisedNextProtocolsItem {
    unsigned char* protocols;
    unsigned length;
  };

  /**
   * Wire-format list of advertised protocols for use in NPN.
   */
  std::vector<AdvertisedNextProtocolsItem> advertisedNextProtocols_;
  std::vector<int> advertisedNextProtocolWeights_;
  std::discrete_distribution<int> nextProtocolDistribution_;

  static int advertisedNextProtocolCallback(
      SSL* ssl, const unsigned char** out, unsigned int* outlen, void* data);

  static int alpnSelectCallback(
      SSL* ssl,
      const unsigned char** out,
      unsigned char* outlen,
      const unsigned char* in,
      unsigned int inlen,
      void* data);

  size_t pickNextProtocols();

  bool alpnAllowMismatch_{true};

  static int passwordCallback(char* password, int size, int, void* data);

  /**
   * The function that will be called directly from openssl
   * in order for the application to get the tlsext_hostname just after
   * parsing the Client Hello or Server Hello message. It will then call
   * the serverNameCb_ function object. Hence, it is sort of a
   * wrapper/proxy between serverNameCb_ and openssl.
   *
   * The openssl's primary intention is for SNI support, but we also use it
   * generically for performing logic after the Client Hello comes in.
   */
  static int baseServerNameOpenSSLCallback(
      SSL* ssl, int* al /* alert (return value) */, void* data);

  std::string providedCiphersString_;

  void setupCtx(SSL_CTX* ctx);

  std::unique_ptr<SessionLifecycleCallbacks> sessionLifecycleCallbacks_{
      nullptr};

  static int newSessionCallback(SSL* ssl, SSL_SESSION* session);
};

/// Shared-ownership pointer to an SSLContext.
using SSLContextPtr = std::shared_ptr<SSLContext>;

} // namespace folly
