/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：保留 JPEG 错误返回和清理路径，不打印平台日志。/ English: Preserve JPEG error returns and cleanup without platform logs.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#define ESP_GOTO_ON_FALSE(value, code, target, ...) do { if (!(value)) { ret = (code); goto target; } } while (0)
#define ESP_RETURN_ON_FALSE(value, code, ...) do { if (!(value)) return (code); } while (0)
