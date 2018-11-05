//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "MainWindow.h"
#include <pumex/platform/qt/WindowQT.h>
#include <QAction>
#include <QLayout>
#include <QStatusBar>
#include <QTextEdit>
#include <QFile>
#include <QDataStream>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent, Qt::WindowFlags flags)
  : QMainWindow(parent, flags)
{
  setObjectName("MainWindow");
  setWindowTitle("Qt Main Window Example");
  
  vulkanWindow = new pumex::QWindowPumex();
  vulkanContainer = QWidget::createWindowContainer(vulkanWindow,this,flags);
  vulkanContainer->setMinimumSize(300,200);
  setCentralWidget(vulkanContainer);

  setupToolBar();
  statusBar()->showMessage(tr("Status Bar"));
}

MainWindow::~MainWindow()
{
  delete vulkanContainer;
}

pumex::QWindowPumex* MainWindow::getVulkanWindow()
{
  return vulkanWindow;
}

void MainWindow::actionTriggered(QAction *action)
{
  qDebug("action '%s' triggered", action->text().toLocal8Bit().data());
}

void MainWindow::setupToolBar()
{
  //for (int i = 0; i < 3; ++i) 
  //{
  //  ToolBar *tb = new ToolBar(QString::fromLatin1("Tool Bar %1").arg(i + 1), this);
  //  toolBars.append(tb);
  //  addToolBar(tb);
  //}
}
