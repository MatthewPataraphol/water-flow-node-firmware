#pragma once

// Synthetic unit-test inputs only: no real account, network or credential.
// No Purdue SSID or credentials are needed by these tests.
#ifndef TEST_WIFI_ENTERPRISE
#define TEST_WIFI_ENTERPRISE 0
#endif
#define CONFIG_FLOW_WIFI_AUTH_ENTERPRISE TEST_WIFI_ENTERPRISE

#if TEST_WIFI_EMPTY_SSID
#define CONFIG_FLOW_WIFI_SSID ""
#elif TEST_WIFI_LONG_SSID
#define CONFIG_FLOW_WIFI_SSID "123456789012345678901234567890123"
#elif TEST_WIFI_PERSONAL_LIMITS
#define CONFIG_FLOW_WIFI_SSID "12345678901234567890123456789012"
#else
#define CONFIG_FLOW_WIFI_SSID "unit-test-network"
#endif

#if TEST_WIFI_PERSONAL_LIMITS
#define CONFIG_FLOW_WIFI_PASSWORD "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#else
#define CONFIG_FLOW_WIFI_PASSWORD "unit-test-personal-password"
#endif

#if TEST_WIFI_INVALID_USERNAME
#define CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME ""
#else
#define CONFIG_FLOW_WIFI_ENTERPRISE_USERNAME "unit-test-user"
#endif
#if TEST_WIFI_IDENTITY_OVERRIDE
#define CONFIG_FLOW_WIFI_ENTERPRISE_IDENTITY "unit-test-outer-identity"
#else
#define CONFIG_FLOW_WIFI_ENTERPRISE_IDENTITY ""
#endif
#if TEST_WIFI_EMPTY_PASSWORD
#define CONFIG_FLOW_WIFI_ENTERPRISE_PASSWORD ""
#else
// Deliberately longer than a Personal-mode station PSK field.
#define CONFIG_FLOW_WIFI_ENTERPRISE_PASSWORD "unit-test-enterprise-password-which-is-deliberately-longer-than-64-bytes"
#endif
#if TEST_WIFI_IDENTITY_OVERRIDE
#define CONFIG_FLOW_WIFI_ENTERPRISE_SERVER_NAME "radius.unit-test.invalid"
#else
#define CONFIG_FLOW_WIFI_ENTERPRISE_SERVER_NAME ""
#endif
