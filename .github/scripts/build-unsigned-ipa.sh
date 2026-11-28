#!/usr/bin/env bash
#
# Builds an UNSIGNED device .ipa on macOS, with no Apple developer account.
#
# `asuka build --platform ios` refuses to build without a provisioning profile
# and signs what it builds. So this makes a throwaway self-signed identity and
# a stub profile that authorizes it (the same trick Quire's Go build uses),
# lets asuka build the device .ipa with them, then strips the signature and
# the profile from the bundle and packs Payload/ again.
#
# The result installs through a re-signing tool — Sideloadly or AltStore with a
# free Apple ID (7 days at a time) — or on a jailbroken device / TrollStore
# (iOS 17.0 and older).
#
# Needs: Xcode 26 (asuka requires it), the asuka CLI, an unlocked keychain.
#
#   asuka: https://asuka.egoist.dev/install.sh
#   xcodebuild -version
#
# Usage: .github/scripts/build-unsigned-ipa.sh [app-dir]

set -euo pipefail

app_dir="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
app_dir="$(cd "$app_dir" && pwd)"
work="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/asuka-unsigned"
uuid="$(uuidgen)"
team="ASUKA00000" # a stand-in: it only exists inside the stub profile

asuka="${ASUKA:-$(command -v asuka || echo "$HOME/.asuka/bin/asuka")}"
if [ ! -x "$asuka" ]; then
  echo "asuka: no CLI; install it: curl -fsSL https://asuka.egoist.dev/install.sh | sh" >&2
  exit 1
fi
if ! command -v xcodebuild >/dev/null; then
  echo "asuka: no Xcode; the iOS build needs Xcode 26 or newer" >&2
  exit 1
fi

# asuka.toml: id = "dev.laix.quire"
id="$(sed -n 's/^[[:space:]]*id[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$app_dir/asuka.toml" | head -1)"
if [ -z "$id" ]; then
  echo "asuka: asuka.toml has no id" >&2
  exit 1
fi
echo "app: $id in $app_dir"

echo "::group::Throwaway signing identity"
rm -rf "$work"
mkdir -p "$work"
cd "$work"

cat >openssl.cnf <<'EOF'
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = Asuka Unsigned Signer
O = Asuka Unsigned
[ext]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
subjectKeyIdentifier = hash
EOF

openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -config openssl.cnf \
  -keyout key.pem -out cert.pem 2>/dev/null

pass="$(openssl rand -hex 12)"
# macOS's LibreSSL writes the legacy PBE `security import` reads; OpenSSL 3
# needs -legacy for the same file.
if ! openssl pkcs12 -export -inkey key.pem -in cert.pem -out signer.p12 \
  -passout "pass:$pass" -legacy 2>/dev/null; then
  openssl pkcs12 -export -inkey key.pem -in cert.pem -out signer.p12 \
    -passout "pass:$pass"
fi
echo "::endgroup::"

echo "::group::Keychain of this run"
keychain="$work/signing.keychain-db"
security create-keychain -p "$pass" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$pass" "$keychain"
security import signer.p12 -k "$keychain" -P "$pass" \
  -T /usr/bin/codesign -T /usr/bin/security >/dev/null
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$pass" \
  "$keychain" >/dev/null
security list-keychains -d user -s "$keychain" login.keychain
# Trusted, so codesign accepts the identity as a signing one.
sudo security add-trusted-cert -d -r trustRoot -p codeSign \
  -k /Library/Keychains/System.keychain "$work/cert.pem"
security find-identity -v -p codesigning
echo "::endgroup::"

echo "::group::Stub provisioning profile"
# asuka reads it as Apple's: a CMS-signed plist. app-store kind (no
# get-task-allow, no ProvisionedDevices), the app's ID under our stand-in team,
# and the certificate above, by SHA-1.
der="$(openssl x509 -in cert.pem -outform DER | base64)"
cat >profile.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Name</key><string>Asuka Unsigned</string>
	<key>UUID</key><string>$uuid</string>
	<key>TeamIdentifier</key><array><string>$team</string></array>
	<key>ExpirationDate</key><date>2036-01-01T00:00:00Z</date>
	<key>DeveloperCertificates</key><array><data>$der</data></array>
	<key>Entitlements</key>
	<dict>
		<key>application-identifier</key><string>$team.$id</string>
		<key>com.apple.developer.team-identifier</key><string>$team</string>
	</dict>
</dict>
</plist>
EOF
openssl smime -sign -binary -in profile.plist -signer cert.pem -inkey key.pem \
  -outform DER -nodetach -out profile.mobileprovision
mkdir -p "$HOME/Library/MobileDevice/Provisioning Profiles"
cp profile.mobileprovision \
  "$HOME/Library/MobileDevice/Provisioning Profiles/asuka-unsigned.mobileprovision"
echo "::endgroup::"

echo "::group::Build the device .ipa"
cd "$app_dir"
"$asuka" check
"$asuka" build --platform ios --profile "$work/profile.mobileprovision"
built="$(ls dist/*.ipa | head -1)"
echo "built $built"
echo "::endgroup::"

echo "::group::Strip the signature"
unpacked="$work/ipa"
rm -rf "$unpacked"
mkdir -p "$unpacked"
ditto -x -k "$built" "$unpacked"
app="$(ls -d "$unpacked"/Payload/*.app)"
rm -rf "$app/_CodeSignature" "$app/embedded.mobileprovision"
exe="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Info.plist")"
if ! codesign --remove-signature "$app/$exe"; then
  echo "note: codesign could not remove the executable's signature" >&2
fi
while IFS= read -r framework; do
  name="$(basename "$framework" .framework)"
  codesign --remove-signature "$framework/$name" 2>/dev/null || true
  rm -rf "$framework/_CodeSignature"
done < <(find "$app" -mindepth 1 -maxdepth 3 -name "*.framework" -type d)
while IFS= read -r dylib; do
  codesign --remove-signature "$dylib" 2>/dev/null || true
  rm -rf "$(dirname "$dylib")/_CodeSignature"
done < <(find "$app" -name "*.dylib" -type f)
echo "::endgroup::"

echo "::group::Give the app legado:// too"
# asuka 0.2's `scheme` writes one scheme, and its `[ios.infoPlist]` takes
# strings only. Book-source sites link both yuedu://… and legado://…, so the
# second one is added here, beside what the build made.
pb=/usr/libexec/PlistBuddy
info="$app/Info.plist"
$pb -c 'Delete :CFBundleURLTypes' "$info" 2>/dev/null || true
$pb -c 'Add :CFBundleURLTypes array' "$info"
$pb -c 'Add :CFBundleURLTypes:0 dict' "$info"
$pb -c "Add :CFBundleURLTypes:0:CFBundleURLName string $id" "$info"
$pb -c 'Add :CFBundleURLTypes:0:CFBundleURLSchemes array' "$info"
$pb -c 'Add :CFBundleURLTypes:0:CFBundleURLSchemes:0 string yuedu' "$info"
$pb -c 'Add :CFBundleURLTypes:0:CFBundleURLSchemes:1 string legado' "$info"
$pb -c 'Print :CFBundleURLTypes' "$info"
echo "::endgroup::"

out="$app_dir/dist/$(basename "${built%.ipa}")-unsigned.ipa"
rm -f "$out"
(cd "$unpacked" && ditto -c -k --sequesterRsrc --keepParent Payload "$out")
rm -f "$built"

# The signature Apple (or a re-signing tool) gives it is what matters: the
# throwaway one is gone, and the ad-hoc one ld64 writes on arm64 is not a
# signature a phone accepts.
if otool -l "$app/$exe" | grep -q LC_CODE_SIGNATURE; then
  echo "unsigned for our purposes: the linker's ad-hoc signature stays, no profile, no identity"
else
  echo "the executable has no signature at all"
fi
ls -la "$out"
