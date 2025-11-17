#pragma once

#include "NotifierConnection.h"

#include <QObject>

#include <filesystem>
#include <vector>

namespace tb::ui
{
class MapDocument;

class MaterialWadFilter : public QObject
{
  Q_OBJECT
private:
  MapDocument& m_document;
  NotifierConnection m_notifierConnection;
  std::vector<std::filesystem::path> m_wadCollections;
  std::vector<std::filesystem::path> m_hiddenWads;

public:
  explicit MaterialWadFilter(MapDocument& document, QObject* parent = nullptr);

  const std::vector<std::filesystem::path>& wadCollections() const;
  const std::vector<std::filesystem::path>& hiddenWads() const;
  bool isWadHidden(const std::filesystem::path& wad) const;

public slots:
  void setWadHidden(const std::filesystem::path& wad, bool hidden);

signals:
  void wadCollectionsChanged();
  void hiddenWadsChanged();

private:
  void connectObservers();
  void reloadCollections();
  void loadHiddenFromPreferences();
  void preferenceDidChange(const std::filesystem::path& path);

  std::vector<std::filesystem::path> gatherWadCollections() const;
};

} // namespace tb::ui
