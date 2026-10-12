# App Store Connect API 密钥口径（TestFlight 用）。只放编号，不放密钥：
#   ~/.appstoreconnect/dm.env：ASC_KEY_ID=… ASC_ISSUER=… ASC_APP_ID=…（App 的数字编号）
#   密钥文件：~/.appstoreconnect/private_keys/AuthKey_<ASC_KEY_ID>.p8（600；altool 缺省也在这里找）
# asc_auth：有密钥就把 xcodebuild 的 API 认证参数放进数组 ASC_AUTH，没有就留空（开发机照旧用本地描述文件）
# App Store Connect API key conventions (for TestFlight). Only IDs go here, not the key:
#   ~/.appstoreconnect/dm.env: ASC_KEY_ID=... ASC_ISSUER=... ASC_APP_ID=... (the App's numeric ID)
#   Key file: ~/.appstoreconnect/private_keys/AuthKey_<ASC_KEY_ID>.p8 (600; altool also looks here by default)
# asc_auth: if a key exists, put the xcodebuild API authentication arguments into the array ASC_AUTH; otherwise leave it empty (development machines keep using local provisioning profiles)
ASC_ENV=${DM_ASC_ENV:-$HOME/.appstoreconnect/dm.env}
asc_auth() {
  ASC_AUTH=()
  [ -f "$ASC_ENV" ] || return 0
  source "$ASC_ENV"
  ASC_KEY=$HOME/.appstoreconnect/private_keys/AuthKey_${ASC_KEY_ID}.p8
  [ -f "$ASC_KEY" ] || { echo "🔴 缺 API 密钥文件 $ASC_KEY"; return 1; }
  ASC_AUTH=(-authenticationKeyPath "$ASC_KEY" -authenticationKeyID "$ASC_KEY_ID" -authenticationKeyIssuerID "$ASC_ISSUER")
}
