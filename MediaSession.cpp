/*
 * Copyright 2016-2017 TATA ELXSI
 * Copyright 2016-2017 Metrological
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

#include "MediaSession.h"
#include "Policy.h"

#include <assert.h>
#include <iostream>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <sstream>
#include <string>
#include <string.h>
#include <sys/utsname.h>

#define NYI_KEYSYSTEM "keysystem-placeholder"
#define IV_FIX_WORKAROUND 1

using namespace std;

namespace CDMi {

static void hex_print(const void *pv, size_t len) {
  const unsigned char *p = (const unsigned char*)pv;
  if (!pv)
    printf("NULL");
  else
  {
    size_t i = 0;
    for (; i<len;++i)
      printf("%02X ", *p++);
  }
  printf("\n");
}

MediaKeySession::MediaKeySession(Cdm *cdm, int32_t licenseType)
    : m_cdm(cdm)
    , m_CDMData("")
    , m_initData("")
    , m_initDataType(Cdm::kCenc)
    , m_licenseType((widevine::Cdm::SessionType)licenseType)
    , m_sessionId("") {
  m_cdm->createSession(m_licenseType, &m_sessionId);
}

MediaKeySession::~MediaKeySession(void) {
}


void MediaKeySession::Run(const IMediaKeySessionCallback *f_piMediaKeySessionCallback) {
  printf("called with callback %p\n", f_piMediaKeySessionCallback);

  if (f_piMediaKeySessionCallback) {
    m_piCallback = const_cast<IMediaKeySessionCallback*>(f_piMediaKeySessionCallback);
  }

  Cdm::Status status = m_cdm->generateRequest(m_sessionId, m_initDataType, m_initData);
  if (Cdm::kSuccess == status)
     printf("generateRequest successful\n");
  else
     printf("generateRequest failed\n");
}

void MediaKeySession::onMessage(const std::string& f_sessionId, Cdm::MessageType f_messageType, const std::string& f_message) {
  std::string destUrl;
  std::string message;

  printf("called\n");

  switch (f_messageType) {
  case Cdm::kLicenseRequest:
  {
    printf("message is a license request\n");
    destUrl.assign(kLicenseServer); 
    Cdm::MessageType messageType = Cdm::kLicenseRequest;

    // FIXME: Errrr, this is weird.
    //if ((Cdm::MessageType)f_message[1] == (Cdm::kIndividualizationRequest + 1)) {
    //  LOGI("switching message type to kIndividualizationRequest");
    //  messageType = Cdm::kIndividualizationRequest;
    //}
    
    message = std::to_string(messageType) + ":Type:";
    break;
  }
  default:
    printf("unsupported message type\n");
    break;
  }
  message.append(f_message.c_str(),  f_message.size());
  m_piCallback->OnKeyMessage((const uint8_t*) message.c_str(), message.size(), (char*) destUrl.c_str());
}

void MediaKeySession::onKeyStatusesChange(const std::string& f_sessionId) {
  Cdm::KeyStatusMap map;
  std::string keyStatus;
  if (Cdm::kSuccess == m_cdm->getKeyStatuses(f_sessionId, &map)) {
    switch (map.begin()->second) {
    case Cdm::kUsable:
      keyStatus = "KeyUsable";
      break;
    case Cdm::kExpired:
      keyStatus = "KeyExpired";
      break;
    case Cdm::kOutputRestricted:
      keyStatus = "KeyOutputRestricted";
      break;
    case Cdm::kStatusPending:
      keyStatus = "KeyStatusPending";
      break;
    case Cdm::kInternalError:
      keyStatus = "KeyInternalError";
      break;
    case Cdm::kReleased:
      keyStatus = "KeyReleased";
      break;
    default:
      keyStatus = "UnKnownError";
      break;
    }
  }
  m_piCallback->OnKeyStatusUpdate(keyStatus.c_str());
}


void MediaKeySession::onKeyStatusError(const std::string& f_sessionId, Cdm::Status status) {
  std::string errorStatus;
  switch (status) {
  case Cdm::kNeedsDeviceCertificate:
    errorStatus = "NeedsDeviceCertificate";
    break;
  case Cdm::kSessionNotFound:
    errorStatus = "SessionNotFound";
    break;
  case Cdm::kDecryptError:
    errorStatus = "DecryptError";
    break;
  case Cdm::kInvalidAccess:
    errorStatus = "InvalidAccess";
    break;
  case Cdm::kQuotaExceeded:
    errorStatus = "QuotaExceeded";
    break;
  case Cdm::kNotSupported:
    errorStatus = "NotSupported";
    break;
  default:
    errorStatus = "UnExpectedError";
    break;
  }
  m_piCallback->OnKeyError(0, CDMi_S_FALSE, errorStatus.c_str());
}

void MediaKeySession::onRemoveComplete(const std::string& f_sessionId) {
  m_piCallback->OnKeyStatusUpdate("KeyReleased");
}

CDMi_RESULT MediaKeySession::Load(void) {
  CDMi_RESULT ret = CDMi_S_FALSE;
  Cdm::Status status = m_cdm->load(m_sessionId);
  if (Cdm::kSuccess != status)
    onKeyStatusError(m_sessionId, status);
  else
    ret = CDMi_SUCCESS;
  return ret;
}

void MediaKeySession::Update(
    const uint8_t *f_pbKeyMessageResponse,
    uint32_t f_cbKeyMessageResponse) {
  std::string keyResponse(reinterpret_cast<const char*>(f_pbKeyMessageResponse),
      f_cbKeyMessageResponse);
  if (Cdm::kSuccess != m_cdm->update(m_sessionId, keyResponse))
     onKeyStatusesChange(m_sessionId);
}

CDMi_RESULT MediaKeySession::Remove(void) {
  CDMi_RESULT ret = CDMi_S_FALSE;
  Cdm::Status status = m_cdm->remove(m_sessionId);
  if (Cdm::kSuccess != status)
    onKeyStatusError(m_sessionId, status);
  else
    ret =  CDMi_SUCCESS;
  return ret;
}

CDMi_RESULT MediaKeySession::Close(void) {
  CDMi_RESULT status = CDMi_S_FALSE;
  if (Cdm::kSuccess == m_cdm->close(m_sessionId))
    status = CDMi_SUCCESS;
  return status;
}

const char* MediaKeySession::GetSessionId(void) const {
  return m_sessionId.c_str();
}

const char* MediaKeySession::GetKeySystem(void) const {
  return NYI_KEYSYSTEM;//TODO: replace with keysystem and test
}

CDMi_RESULT MediaKeySession::Init(
    int32_t licenseType,
    const char *f_pwszInitDataType,
    const uint8_t *f_pbInitData,
    uint32_t f_cbInitData,
    const uint8_t *f_pbCDMData,
    uint32_t f_cbCDMData) {
  switch ((LicenseType)licenseType) {
  case PersistentUsageRecord:
    m_licenseType = Cdm::kPersistentUsageRecord;
    break;
  case PersistentLicense:
    m_licenseType = Cdm::kPersistentLicense;
    break;
  default:
    m_licenseType = Cdm::kTemporary;
    break;
  }

  if (f_pwszInitDataType) {
    if (!strcmp(f_pwszInitDataType, "cenc"))
       m_initDataType = Cdm::kCenc;
    else if (!strcmp(f_pwszInitDataType, "webm"))
       m_initDataType = Cdm::kWebM;
  }

  if (f_pbInitData && f_cbInitData)
    m_initData.assign((const char*) f_pbInitData, f_cbInitData);

  if (f_pbCDMData && f_cbCDMData)
    m_CDMData.assign((const char*) f_pbCDMData, f_cbCDMData);
  return CDMi_SUCCESS;
}

CDMi_RESULT MediaKeySession::Decrypt(
    const uint8_t *f_pbSessionKey,
    uint32_t f_cbSessionKey,
    const uint32_t *f_pdwSubSampleMapping,
    uint32_t f_cdwSubSampleMapping,
    const uint8_t *f_pbIV,
    uint32_t f_cbIV,
    const uint8_t *f_pbData,
    uint32_t f_cbData,
    uint32_t *f_pcbOpaqueClearContent,
    uint8_t **f_ppbOpaqueClearContent) {
  Cdm::KeyStatusMap map;
  std::string keyStatus;

  CDMi_RESULT status = CDMi_S_FALSE;
  *f_pcbOpaqueClearContent = 0;

  /* Added a workaround to fix IV issue. Will revert once we have proper fix */
#ifdef IV_FIX_WORKAROUND
  unsigned char iv[16];
  memset(iv,0,16);
  memcpy(iv,(char*)f_pbIV, f_cbIV);
#endif

  if (Cdm::kSuccess == m_cdm->getKeyStatuses(m_sessionId, &map)) {
    Cdm::KeyStatusMap::iterator it = map.begin();
    // FIXME: We just check the first key? How do we know that's the Widevine key and not, say, a PlayReady one?
    if (Cdm::kUsable == it->second) {
      Cdm::OutputBuffer output;
      uint8_t *outputBuffer = (uint8_t*) malloc(f_cbData * sizeof(uint8_t));
      output.data = outputBuffer;
      output.data_length = f_cbData;
      Cdm::InputBuffer input;
      input.data = f_pbData;
      input.data_length = f_cbData;
      input.key_id = reinterpret_cast<const uint8_t*>((it->first).c_str());
      input.key_id_length = (it->first).size();
#ifndef IV_FIX_WORKAROUND /* Added a workaround to fix IV issue. Will revert once we have proper fix */
      input.iv = f_pbIV;
      input.iv_length = f_cbIV;
#else
      input.iv = iv;
      input.iv_length = sizeof(iv);
#endif
      input.is_encrypted = true;
      if (Cdm::kSuccess == m_cdm->decrypt(input, output)) {
        /* Return clear content */
        *f_pcbOpaqueClearContent = output.data_length;
        *f_ppbOpaqueClearContent = outputBuffer;
        status = CDMi_SUCCESS;
      }
    }
  }


  return status;
}

CDMi_RESULT MediaKeySession::ReleaseClearContent(
    const uint8_t *f_pbSessionKey,
    uint32_t f_cbSessionKey,
    const uint32_t  f_cbClearContentOpaque,
    uint8_t  *f_pbClearContentOpaque ){
  CDMi_RESULT ret = CDMi_S_FALSE;
  if (f_pbClearContentOpaque) {
    free(f_pbClearContentOpaque);
    ret = CDMi_SUCCESS;
  }
  return ret;
}
}  // namespace CDMi