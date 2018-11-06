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
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLayout>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QColorDialog>
#include <QtCore/QDebug>

MainWindow::MainWindow(pumex::QWindowPumex* pumexWindow)
  : modelColor(255, 255, 255)
{
  QWidget *wrapper = QWidget::createWindowContainer(pumexWindow);
  wrapper->setMinimumSize(600, 400);
  wrapper->setFocusPolicy(Qt::StrongFocus);
  wrapper->setFocus();

  btnSetModelColor = new QPushButton(tr("Set model &color ..."));
  btnSetModelColor->setFocusPolicy(Qt::NoFocus);
  btnLoadModel     = new QPushButton(tr("Load &model ..."));
  btnLoadModel->setFocusPolicy(Qt::NoFocus);
  btnLoadAnimation = new QPushButton(tr("Load &animation ..."));
  btnLoadAnimation->setFocusPolicy(Qt::NoFocus);
  btnQuit          = new QPushButton(tr("&Quit"));
  btnQuit->setFocusPolicy(Qt::NoFocus);

  connect(btnSetModelColor, &QPushButton::clicked, this, &MainWindow::setModelColor);
  connect(btnLoadModel,     &QPushButton::clicked, this, &MainWindow::loadModel);
  connect(btnLoadAnimation, &QPushButton::clicked, this, &MainWindow::loadAnimation);
  connect(btnQuit,          &QPushButton::clicked, this, &QWidget::close);

  setWindowTitle("Pumex using QT window : just some sphere model");
  
  QGridLayout *layout = new QGridLayout;
  layout->addWidget( btnSetModelColor, 3, 0 );
  layout->addWidget( btnLoadModel,     4, 0 );
  layout->addWidget( btnLoadAnimation, 5, 0 );
  layout->addWidget( btnQuit,          6, 0 );
  layout->addWidget( wrapper, 0, 1, 7, 4);
  setLayout(layout);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setModelColor()
{
  QColor newColor = QColorDialog::getColor(modelColor, this, "Select model color");
  if (!newColor.isValid())
    return;
  modelColor = newColor;
  emit signalSetModelColor( glm::vec4(
    (float)modelColor.red() / 255.0f,
    (float)modelColor.green() / 255.0f,
    (float)modelColor.blue() / 255.0f,
    1.0f
  ));
}

void MainWindow::loadModel()
{
  QString fileName = QFileDialog::getOpenFileName(this, tr("Open model file"), "", tr("All files (*.*)"));
  if (fileName.isEmpty())
    return;
  setWindowTitle(QString("Pumex using QT window : ") + fileName);
  emit signalLoadModel(fileName.toStdString());
}

void MainWindow::loadAnimation()
{
  QString fileName = QFileDialog::getOpenFileName(this, tr("Open animation file"), "", tr("All files (*.*)"));
  if (fileName.isEmpty())
    return;
  emit signalLoadAnimation(fileName.toStdString());
}
