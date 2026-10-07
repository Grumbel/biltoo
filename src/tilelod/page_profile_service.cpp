// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilelod/page_profile_service.hpp"

#include "host/pagepath.h"
#include "host/thumtoocache.h"
#include "tilelod/tile_lod_registry.hpp"

#include <thumtoo/pdf.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QThreadPool>

namespace tilelod {

namespace {

SourceKind classify(QString const& path)
{
  if (!PagePath::isPageRef(path) && !PagePath::isTextForceRef(path)) {
    return SourceKind::Image;
  }
  PagePath::Ref const ref = PagePath::parse(path);
  if (ref.isEpub()) {
    return SourceKind::EpubPage;
  }
  if (PagePath::isDjvuFile(ref.pdfPath)) {
    return SourceKind::DjvuPage;
  }
  return SourceKind::PdfPage;
}

}  // namespace

PageProfileService& PageProfileService::instance()
{
  static PageProfileService service;
  return service;
}

std::int64_t PageProfileService::now_ms()
{
  return QDateTime::currentMSecsSinceEpoch();
}

SourceRecord const& PageProfileService::observe(QString const& path, bool want_profile)
{
  std::string const key = path.toStdString();
  SourceRecords& records = SourceRecords::instance();
  if (SourceRecord const* r = records.find(key)) {
    if (r->profile_state != ProfileState::Unknown || !want_profile) {
      return *r;
    }
  }
  SourceRecord& rec = records.ensure(key, classify(path));
  if (want_profile && rec.profile_state == ProfileState::Unknown) {
    request(rec);
  }
  return rec;
}

void PageProfileService::request(SourceRecord& rec)
{
  QString const path = QString::fromStdString(rec.path);
  auto const ref = ThumtooCache::pdfPageForPath(path);
  if (!ref) {
    rec.profile_state = ProfileState::Failed;
    rec.profile_error = "path does not map to a MuPDF page";
    SourceRecords::instance().touch();
    return;
  }
  rec.document_file = ref->file;
  rec.page = ref->page;
  static std::uint64_t s_next_request = 0;
  rec.profile_state = ProfileState::Pending;
  rec.profile_requested_ms = now_ms();
  rec.profile_request = ++s_next_request;
  SourceRecords::instance().touch();

  std::string const uri = ref->uri;
  std::string const key = rec.path;
  std::uint64_t const request = rec.profile_request;
  // Own small pool: profiles of one document serialize on its lock in
  // thumtoo; on the global pool they would park decode threads.
  static QThreadPool* pool = [] {
    auto* p = new QThreadPool(QCoreApplication::instance());
    p->setMaxThreadCount(2);
    return p;
  }();
  pool->start([uri, key, path, request]() {
    std::string error;
    std::optional<thumtoo::PdfPageProfile> profile;
    // Parse on this thread: //text pages arm MuPDF's text open per thread.
    if (auto parsed = thumtoo::parse_pdf_uri(uri)) {
      profile = thumtoo::pdf_page_profile(parsed->pdf_path, parsed->page, &error);
    } else {
      error = "not a PDF page URI: " + uri;
    }
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [key, path, request, profile = std::move(profile), error = std::move(error)]() {
          SourceRecord* r = SourceRecords::instance().find_mutable(key);
          if (!r || r->profile_request != request) {
            return;  // forgotten (file changed / session replaced) meanwhile
          }
          r->profile_ms = now_ms() - r->profile_requested_ms;
          if (profile) {
            r->profile = profile;
            r->profile_state = ProfileState::Known;
            r->profile_error.clear();
          } else {
            r->profile_state = ProfileState::Failed;
            r->profile_error = error.empty() ? "no profile" : error;
          }
          SourceRecords::instance().touch();
          // The zoom floor may have moved: let every view of the path re-plan.
          TileLodRegistry::instance().notify_path_views(path);
        },
        Qt::QueuedConnection);
  });
}

}  // namespace tilelod
