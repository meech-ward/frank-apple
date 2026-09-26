# Amazon Trust Services roots

`amazon-roots.pem` contains unmodified Amazon Root CA 1, 2, 3 and 4 PEM
certificates from https://www.amazontrust.com/repository/, downloaded 2026-09-22.
Their DER SHA-256 fingerprints were checked against that page. These extend the
existing Google/Let's Encrypt trust bundle for HTTPS hosts including httpbin.org.

Certificates are provided by Amazon Trust Services under
[CC BY-ND 4.0](https://creativecommons.org/licenses/by-nd/4.0/).
Source files: `https://www.amazontrust.com/repository/AmazonRootCA{1,2,3,4}.pem`.
