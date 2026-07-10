#pragma once

#include <QAbstractListModel>
#include <QString>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cockpit/core/base/macros.h"

namespace cockpit {
namespace ui {

struct ServiceHealthEndpoint {
  QString display_name;
  std::string service_name;
  std::string address;
};

class ServiceHealthModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int okCount READ okCount NOTIFY summaryChanged)
  Q_PROPERTY(int degradedCount READ degradedCount NOTIFY summaryChanged)
  Q_PROPERTY(int faultedCount READ faultedCount NOTIFY summaryChanged)
  Q_PROPERTY(int unknownCount READ unknownCount NOTIFY summaryChanged)
  Q_PROPERTY(QString summaryText READ summaryText NOTIFY summaryChanged)
  Q_PROPERTY(QString worstState READ worstState NOTIFY summaryChanged)

 public:
  enum Role {
    DisplayNameRole = Qt::UserRole + 1,
    ServiceNameRole,
    StateRole,
    MessageRole,
    LastErrorRole,
    CheckedAtRole,
  };

  struct HealthSample {
    QString state;
    QString message;
    QString last_error;
    qint64 checked_at_ms = 0;
  };

  explicit ServiceHealthModel(std::vector<ServiceHealthEndpoint> endpoints,
                              QObject* parent = nullptr);
  ~ServiceHealthModel() override;

  COCKPIT_DISALLOW_COPY_AND_ASSIGN(ServiceHealthModel);

  int rowCount(const QModelIndex& parent) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  int okCount() const;
  int degradedCount() const;
  int faultedCount() const;
  int unknownCount() const;
  QString summaryText() const;
  QString worstState() const;

  void Start();
  void Stop();

 signals:
  void summaryChanged();

 private:
  struct Item {
    QString display_name;
    std::string service_name;
    std::string address;
    QString state = QStringLiteral("UNKNOWN");
    QString message = QStringLiteral("Waiting for health check");
    QString last_error;
    qint64 checked_at_ms = 0;
  };

  void Run();
  void PollOnce();
  void PostSamples(std::vector<HealthSample> samples);
  void RecountLocked() const;

  mutable std::mutex mutex_;
  std::vector<Item> items_;
  std::atomic_bool running_{false};
  std::thread worker_;
  mutable bool counts_dirty_ = true;
  mutable int ok_count_ = 0;
  mutable int degraded_count_ = 0;
  mutable int faulted_count_ = 0;
  mutable int unknown_count_ = 0;
};

}  // namespace ui
}  // namespace cockpit
