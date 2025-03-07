#include <QGuiApplication>
#include <QScreen>
#include <QDesktopWidget>

#include <QWidget>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QStatusBar>
#include <QMenu>
#include <QActionGroup>
#include <QToolButton>
#include <QCheckBox>
#include <QComboBox>
#include <QTextEdit>
#include <QScrollBar>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QProgressBar>
#include <QRadioButton>


namespace phoenix {

Size pDesktop::size() {
  QRect rect = QGuiApplication::primaryScreen()->geometry();
  return {rect.width(), rect.height()};
}

Geometry pDesktop::workspace() {
  QRect rect = QGuiApplication::primaryScreen()->availableGeometry();
  return {rect.x(), rect.y(), rect.width(), rect.height()};
}

}
