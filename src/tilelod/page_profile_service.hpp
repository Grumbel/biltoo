// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BILTOO_TILELOD_PAGE_PROFILE_SERVICE_HPP
#define BILTOO_TILELOD_PAGE_PROFILE_SERVICE_HPP

#include "tilelod/core.hpp"
#include <thumtoo/lod/source_records.hpp>

#include <QString>

namespace tilelod {

/**
 * The system that fills SourceRecords with page profiles.
 *
 * observe(path) classifies the source on first sight and, for MuPDF pages
 * without a profile, asks thumtoo for one on a worker thread
 * (Unknown → Pending → Known | Failed). When the answer lands (GUI thread)
 * the record is updated and every view of the path re-plans through its
 * loader change hook — the only notification there is.
 *
 * GUI thread only.
 */
class PageProfileService {
public:
  static PageProfileService& instance();

  /// The record for @p path (created if needed). With @p want_profile, a MuPDF
  /// page without a profile gets one requested — views pass true only when
  /// they would plan finer than the layout scale (the profile decides how
  /// far), so fit/Gallery views never queue profile work.
  SourceRecord const& observe(QString const& path, bool want_profile = true);

  /// Wall-clock milliseconds (the clock used for record timestamps).
  static std::int64_t now_ms();

private:
  PageProfileService() = default;
  void request(SourceRecord& record);
};

}  // namespace tilelod

#endif
