﻿// UTF-8 with BOM

// Avoid gibberish when use MSVC
#if _MSC_VER >= 1600
#pragma execution_character_set("utf-8")
#endif

#include "ImageWidget.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <math.h>
#include <string>

// static const var
const QImage ImageWidget::NULL_QIMAGE = QImage();
const QPoint ImageWidget::NULL_POINT = QPoint(0, 0);
const QSize ImageWidget::NULL_SIZE = QSize(0, 0);
const QRect ImageWidget::NULL_RECT = QRect(0, 0, 0, 0);

SelectRect::SelectRect(QWidget* parent)
    : QWidget(parent)
{
    mouseStatus = Qt::NoButton;

    // 初始化右键菜单
    mMenu = new QMenu(this);
    mActionReset = mMenu->addAction(tr("重选")); // Reset
    mActionSaveZoomedImage = mMenu->addAction(tr("另存为(缩放图像)")); // Save as (From the zoomed image)
    mActionSaveOriginalImage = mMenu->addAction(tr("另存为(实际图像)")); // Save as (From the original image)
    mActionExit = mMenu->addAction(tr("退出")); // Exit

    connect(mActionExit, SIGNAL(triggered()), this, SLOT(selectExit()));
    connect(mActionSaveZoomedImage, SIGNAL(triggered()), this, SLOT(cropZoomedImage()));
    connect(mActionSaveOriginalImage, SIGNAL(triggered()), this, SLOT(cropOriginalImage()));
    connect(mActionReset, SIGNAL(triggered()), this, SLOT(selectReset()));
    // 关闭后释放资源
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setFocusPolicy(Qt::StrongFocus);
}

SelectRect::~SelectRect()
{
    image = nullptr;
    zoomedImage = nullptr;
    mMenu = nullptr;
}

void SelectRect::receiveParentSizeChangedSignal()
{
    ImageWidget* parentWidget = static_cast<ImageWidget*>(this->parent());
    this->setGeometry(0, 0, parentWidget->width(), parentWidget->height());
    //    qDebug() << this->geometry();
    drawImageTopLeftPos = parentWidget->getDrawImageTopLeftPos();
    update();
}

void SelectRect::paintEvent(QPaintEvent* event)
{
    // 背景
    QPainterPath mask;
    // 选中的范围
    QPainterPath selectArea;
    // 椭圆
    selectArea.addRect(selectedRect[SR_CENTER].x(), selectedRect[SR_CENTER].y(), selectedRect[SR_CENTER].width(), selectedRect[SR_CENTER].height());
    mask.addRect(this->geometry());
    QPainterPath drawMask = mask.subtracted(selectArea);
    QPainter painter(this);
    painter.setPen(QPen(QColor(255, 0, 0, 255), 1));
    painter.fillPath(drawMask, QBrush(QColor(0, 0, 0, 160)));
    painter.drawRect(selectedRect[SR_CENTER]);
    if (isSelectedRectStable) {
        painter.setPen(QPen(QColor(0, 140, 255, 255), 1));
        painter.drawRect(selectedRect[SR_CENTER]);
        for (int i = 1; i < 5; i++)
            painter.fillRect(selectedRect[i], QBrush(QColor(0, 140, 255, 255)));
    }
}

void SelectRect::mousePressEvent(QMouseEvent* event)
{
    switch (event->button()) {
    case Qt::LeftButton:
        mouseStatus = Qt::LeftButton;
        mouseLeftClickedPos = event->pos();
        // 关闭鼠标追踪 节省资源
        this->setMouseTracking(false);
        break;
    case Qt::RightButton:
        mouseStatus = Qt::RightButton;
        break;
    case Qt::MiddleButton:
        mouseStatus = Qt::MiddleButton;
        break;
    default:
        mouseStatus = Qt::NoButton;
    }
}

void SelectRect::mouseMoveEvent(QMouseEvent* event)
{
    if (mouseStatus == Qt::LeftButton) {
        isSelectedRectStable = false;
        isSelectedRectExisted = true;
        selectedRectChangeEvent(cursorPosInSelectedArea, event->pos());
        update();
    }
    // 判断鼠标是否在矩形框内
    if (mouseStatus == Qt::NoButton && isSelectedRectStable) {
        if (selectedRect[SR_ENTIRETY].contains(event->pos())) {
            cursorPosInSelectedArea = getSelectedAreaSubscript(event->pos());
        } else {
            cursorPosInSelectedArea = SR_NULL;
            this->setCursor(Qt::ArrowCursor);
        }
    }
}

void SelectRect::mouseReleaseEvent(QMouseEvent* event)
{
    if (mouseStatus == Qt::LeftButton) {
        // 修正RectInfo::w和RectInfo::h为正
        fixRectInfo(selectedRect[SR_CENTER]);
        // 备份
        lastSelectedRect = selectedRect[SR_CENTER];
        mouseStatus = Qt::NoButton;
        getEdgeRect();
        isSelectedRectStable = true;
        isSelectedRectExisted = true;
        update();
        // 开启鼠标追踪
        this->setMouseTracking(true);
        mouseStatus = Qt::NoButton;
    }
}

void SelectRect::contextMenuEvent(QContextMenuEvent* event)
{
    mMenu->exec(QCursor::pos());
    mouseStatus = Qt::NoButton;
}

void SelectRect::wheelEvent(QWheelEvent* event) { eventFilter(this, event); }

bool SelectRect::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            // 如果存在选中框则删除选中框 不存在则退出
            if (isSelectedRectExisted) {
                selectReset();
                return true;
            } else
                this->selectExit();
        }
    }
    // 截断wheelEvent
    if (event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        wheelEvent->accept();
        return true;
    }
    return false;
}

void SelectRect::keyPressEvent(QKeyEvent* event) { eventFilter(this, event); }

QRect SelectRect::getRectInImage(const QImage* img, const QPoint& imgTopLeftPos, QRect rect)
{
    QRect returnRect;
    // 计算相对于图像内的坐标
    returnRect.setTopLeft(rect.topLeft() - imgTopLeftPos);
    returnRect.setSize(rect.size());
    // 限定截取范围在图像内 修正顶点
    if (returnRect.x() < 0) {
        // QRect::setX change the width
        returnRect.setX(0);
    }
    if (returnRect.y() < 0) {
        // QRect::setY change the height
        returnRect.setY(0);
    }
    if (returnRect.bottomRight().x() >= img->width())
        // QRect::setRight change the width
        returnRect.setRight(img->width() - 1);
    if (returnRect.bottomRight().y() >= img->height())
        // Qrect::setBottom change the height
        returnRect.setBottom(img->height() - 1);
    return returnRect;
}

void SelectRect::getEdgeRect()
{
    // 边框宽
    int w = 5;

    selectedRect[SR_TOPLEFT].setTopLeft(selectedRect[SR_CENTER].topLeft() + QPoint(-w, -w));
    selectedRect[SR_TOPLEFT].setBottomRight(selectedRect[SR_CENTER].topLeft() + QPoint(-1, -1));

    selectedRect[SR_TOPRIGHT].setTopRight(selectedRect[SR_CENTER].topRight() + QPoint(w, -w));
    selectedRect[SR_TOPRIGHT].setBottomLeft(selectedRect[SR_CENTER].topRight() + QPoint(1, -1));

    selectedRect[SR_BOTTOMRIGHT].setTopLeft(selectedRect[SR_CENTER].bottomRight() + QPoint(1, 1));
    selectedRect[SR_BOTTOMRIGHT].setBottomRight(selectedRect[SR_CENTER].bottomRight() + QPoint(w, w));

    selectedRect[SR_BOTTOMLEFT].setTopRight(selectedRect[SR_CENTER].bottomLeft() + QPoint(-1, 1));
    selectedRect[SR_BOTTOMLEFT].setBottomLeft(selectedRect[SR_CENTER].bottomLeft() + QPoint(-w, w));

    selectedRect[SR_TOP].setTopLeft(selectedRect[SR_TOPLEFT].topRight() + QPoint(1, 0));
    selectedRect[SR_TOP].setBottomRight(selectedRect[SR_TOPRIGHT].bottomLeft() + QPoint(-1, 0));

    selectedRect[SR_RIGHT].setTopLeft(selectedRect[SR_TOPRIGHT].bottomLeft() + QPoint(0, 1));
    selectedRect[SR_RIGHT].setBottomRight(selectedRect[SR_BOTTOMRIGHT].topRight() + QPoint(0, -1));

    selectedRect[SR_BOTTOM].setTopLeft(selectedRect[SR_BOTTOMLEFT].topRight() + QPoint(1, 0));
    selectedRect[SR_BOTTOM].setBottomRight(selectedRect[SR_BOTTOMRIGHT].bottomLeft() + QPoint(-1, 0));

    selectedRect[SR_LEFT].setTopLeft(selectedRect[SR_TOPLEFT].bottomLeft() + QPoint(0, 1));
    selectedRect[SR_LEFT].setBottomRight(selectedRect[SR_BOTTOMLEFT].topRight() + QPoint(0, -1));

    selectedRect[SR_ENTIRETY].setTopLeft(selectedRect[SR_TOPLEFT].topLeft());
    selectedRect[SR_ENTIRETY].setBottomRight(selectedRect[SR_BOTTOMRIGHT].bottomRight());
}

int SelectRect::getSelectedAreaSubscript(QPoint cursorPos)
{
    // 可用树结构减少if
    if (selectedRect[SR_CENTER].contains(cursorPos)) {
        this->setCursor(Qt::SizeAllCursor);
        return SR_CENTER;
    }
    if (selectedRect[SR_TOPLEFT].contains(cursorPos)) {
        this->setCursor(Qt::SizeFDiagCursor);
        return SR_TOPLEFT;
    }
    if (selectedRect[SR_TOP].contains(cursorPos)) {
        this->setCursor(Qt::SizeVerCursor);
        return SR_TOP;
    }
    if (selectedRect[SR_TOPRIGHT].contains(cursorPos)) {
        this->setCursor(Qt::SizeBDiagCursor);
        return SR_TOPRIGHT;
    }
    if (selectedRect[SR_RIGHT].contains(cursorPos)) {
        this->setCursor(Qt::SizeHorCursor);
        return SR_RIGHT;
    }
    if (selectedRect[SR_BOTTOMRIGHT].contains(cursorPos)) {
        this->setCursor(Qt::SizeFDiagCursor);
        return SR_BOTTOMRIGHT;
    }
    if (selectedRect[SR_BOTTOM].contains(cursorPos)) {
        this->setCursor(Qt::SizeVerCursor);
        return SR_BOTTOM;
    }
    if (selectedRect[SR_BOTTOMLEFT].contains(cursorPos)) {
        this->setCursor(Qt::SizeBDiagCursor);
        return SR_BOTTOMLEFT;
    }
    if (selectedRect[SR_LEFT].contains(cursorPos)) {
        this->setCursor(Qt::SizeHorCursor);
        return SR_LEFT;
    }
    return SR_NULL;
}

void SelectRect::selectedRectChangeEvent(int SR_LOCATION, const QPoint& cursorPos)
{
    int x = cursorPos.x();
    int y = cursorPos.y();
    switch (SR_LOCATION) {
    case SR_NULL:
        // 限定在mask内
        selectedRect[SR_CENTER].setTopLeft(mouseLeftClickedPos);
        if (x < 0)
            x = 0;
        else if (x > this->width())
            x = this->width();
        if (y < 0)
            y = 0;
        else if (y > this->height())
            y = this->height();
        selectedRect[SR_CENTER].setWidth(x - selectedRect[SR_CENTER].x());
        selectedRect[SR_CENTER].setHeight(y - selectedRect[SR_CENTER].y());
        fixRectInfo(selectedRect[SR_CENTER]);
        break;
    case SR_CENTER:
        selectedRect[SR_CENTER].moveTo(lastSelectedRect.topLeft() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_TOPLEFT:
        selectedRect[SR_CENTER].setTopLeft(lastSelectedRect.topLeft() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_TOP:
        selectedRect[SR_CENTER].setTop(lastSelectedRect.top() + (cursorPos.y() - mouseLeftClickedPos.y()));
        break;
    case SR_TOPRIGHT:
        selectedRect[SR_CENTER].setTopRight(lastSelectedRect.topRight() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_RIGHT:
        selectedRect[SR_CENTER].setRight(lastSelectedRect.right() + (cursorPos.x() - mouseLeftClickedPos.x()));
        break;
    case SR_BOTTOMRIGHT:
        selectedRect[SR_CENTER].setBottomRight(lastSelectedRect.bottomRight() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_BOTTOM:
        selectedRect[SR_CENTER].setBottom(lastSelectedRect.bottom() + (cursorPos.y() - mouseLeftClickedPos.y()));
        break;
    case SR_BOTTOMLEFT:
        selectedRect[SR_CENTER].setBottomLeft(lastSelectedRect.bottomLeft() + (cursorPos - mouseLeftClickedPos));
        break;
    case SR_LEFT:
        selectedRect[SR_CENTER].setLeft(lastSelectedRect.left() + (cursorPos.x() - mouseLeftClickedPos.x()));
        break;
    default:
        break;
    }
}

void SelectRect::selectExit()
{
    emit sendSelectModeExit();
    this->close();
}

void SelectRect::selectReset()
{
    selectedRect[SR_CENTER] = QRect(0, 0, 0, 0);
    lastSelectedRect = QRect(0, 0, 0, 0);

    isSelectedRectStable = false;
    isSelectedRectExisted = false;
    // 关闭鼠标追踪 节省资源
    this->setMouseTracking(false);
    this->setCursor(Qt::ArrowCursor);
    cursorPosInSelectedArea = SR_NULL;
    mouseStatus = Qt::NoButton;
    update();
}

void SelectRect::cropZoomedImage()
{
    fixedRectInImage = getRectInImage(zoomedImage, drawImageTopLeftPos, selectedRect[SR_CENTER]);
    saveImage(zoomedImage, fixedRectInImage);
}

void SelectRect::cropOriginalImage()
{
    fixedRectInImage = getRectInImage(zoomedImage, drawImageTopLeftPos, selectedRect[SR_CENTER]);
    int x2 = fixedRectInImage.bottomRight().x();
    int y2 = fixedRectInImage.bottomRight().y();
    fixedRectInImage.setX(int(round(double(fixedRectInImage.x()) / double(zoomedImage->width()) * double(image->width()))));
    fixedRectInImage.setY(int(round(double(fixedRectInImage.y()) / double(zoomedImage->height()) * double(image->height()))));
    fixedRectInImage.setWidth(int(round(double(x2) / double(zoomedImage->width()) * double(image->width()))) - fixedRectInImage.x());
    fixedRectInImage.setHeight(int(round(double(y2) / double(zoomedImage->height()) * double(image->height()))) - fixedRectInImage.y());
    saveImage(image, fixedRectInImage);
}

void SelectRect::saveImage(const QImage* img, QRect rect)
{
    // 接受到截取框信息
    if (isLoadImage) {
        if (rect.width() > 0 && rect.height() > 0) {
            QImage saveImageTemp = img->copy(rect);
            QString filename
                = QFileDialog::getSaveFileName(this, tr("Save File"), QCoreApplication::applicationDirPath(), tr("Images (*.png *.xpm *.jpg *.tiff *.bmp)"));
            if (!filename.isEmpty() || !filename.isNull())
                saveImageTemp.save(filename);
        } else {
            QMessageBox msgBox(QMessageBox::Critical, tr("错误!"), tr("未选中图像!")); // Error! No Image Selected!
            msgBox.exec();
        }
    } else {
        QMessageBox msgBox(QMessageBox::Critical, tr("错误!"), tr("未选中图像!")); // Error! No Image Selected!
        msgBox.exec();
    }
}

void SelectRect::fixRectInfo(QRect& rect)
{
    QPoint topLeft = rect.topLeft();
    int width = rect.width();
    int height = rect.height();
    if (width < 0) {
        topLeft.setX(topLeft.x() + width);
        width = -width;
    }
    if (height < 0) {
        topLeft.setY(topLeft.y() + height);
        height = -height;
    }
    rect.setTopLeft(topLeft);
    rect.setSize(QSize(width, height));
}

ImageWidget::ImageWidget(QWidget* parent)
    : QWidget(parent)
{
    isSelectMode = false;
    imageWidgetPaintRect = QRect(-PAINT_AREA_OFFEST, -PAINT_AREA_OFFEST, this->width() + 2 * PAINT_AREA_OFFEST, this->height() + 2 * PAINT_AREA_OFFEST);
    initializeContextmenu();
}

ImageWidget::~ImageWidget() {}

bool ImageWidget::setImage(const QImage& img, bool isDeepCopy)
{
    if (img.isNull()) {
        return false;
    }
    // 默认使用QImage的浅拷贝，自动管理QImage中data引用，避免使用指针，传入局部变量造成crash
    if (isDeepCopy) {
        inputImg = img.copy();
    } else {
        inputImg = img;
    }
    initShowImage();
    return true;
}

bool ImageWidget::setImage(const QString& filePath) { return loadImageFromPath(filePath); }

bool ImageWidget::setImage(const std::string& filePath) { return loadImageFromPath(QString::fromStdString(filePath)); }

bool ImageWidget::loadImageFromPath(const QString& filePath)
{
    if (filePath.isEmpty() || filePath.isNull()) {
        return false;
    }
    inputImg.load(filePath);
    if (inputImg.isNull()) {
        return false;
    }
    initShowImage();
    return true;
}

void ImageWidget::setImageAttributeWithAutoFitFlag(bool enableAutoFit)
{
    if (enableAutoFit) {
        // 根据Widget大小缩放图像
        paintImgSize = inputImg.size();
        zoomScale = paintImgSize.width() / inputImg.width();
        paintImgSize.scale(this->width(), this->height(), Qt::KeepAspectRatio);
        paintImg = inputImg.scaled(paintImgSize);
    } else {
        paintImgSize = paintImg.size();
        zoomScale = 1.0;
        paintImg = inputImg;
    }
    // 计算图像在Widget中显示的左上坐标
    paintImageRect.setTopLeft(getImageTopLeftPosWhenShowInCenter(paintImg, this));
    paintImageRect.setSize(paintImg.size());
    paintImageLastTopLeft = paintImageRect.topLeft();
}

void ImageWidget::initShowImage()
{
    paintImg = inputImg.copy();
    setImageAttributeWithAutoFitFlag(enableAutoFitWidget);
    if (enableLoadImageWithDefaultConfig) {
        setDefaultParameters();
    }
    updateImageWidget();
}

void ImageWidget::clear()
{
    if (!inputImg.isNull()) {
        inputImg = NULL_QIMAGE;
        paintImg = NULL_QIMAGE;
        lastPaintImgSize = NULL_SIZE;
        zoomScale = 1.0;
        mouseLeftKeyPressDownPos = NULL_POINT;
        paintImageLastTopLeft = NULL_POINT;
        paintImageRect = NULL_RECT;
        update();
    }
}

void ImageWidget::setEnableOnlyShowImage(bool flag) { enableOnlyShowImage = flag; }

ImageWidget* ImageWidget::setEnableDrag(bool flag)
{
    enableDragImage = flag;
    mActionEnableDrag->setChecked(enableDragImage);
    return this;
}

ImageWidget* ImageWidget::setEnableZoom(bool flag)
{
    enableZoomImage = flag;
    mActionEnableZoom->setChecked(enableZoomImage);
    return this;
}

ImageWidget* ImageWidget::setEnableAutoFit(bool flag)
{
    enableAutoFitWidget = flag;
    mActionImageAutoFitWidget->setChecked(enableAutoFitWidget);
    resetImageWidget();
    return this;
}

ImageWidget* ImageWidget::setEnableLoadImageWithDefaultConfig(bool flag)
{
    enableLoadImageWithDefaultConfig = flag;
    mActionLoadImageWithDefaultConfig->setChecked(enableLoadImageWithDefaultConfig);
    return this;
}

ImageWidget* ImageWidget::setMaxZoomScale(double scale)
{
    MAX_ZOOM_SCALE = scale;
    return this;
}

ImageWidget* ImageWidget::setMinZoomScale(double scale)
{
    MIN_ZOOM_SCALE = scale;
    return this;
}

ImageWidget* ImageWidget::setMaxZoomedImageSize(int width, int height)
{
    MAX_ZOOMED_IMG_SIZE.setWidth(width);
    MAX_ZOOMED_IMG_SIZE.setHeight(height);
    return this;
}

ImageWidget* ImageWidget::setMinZoomedImageSize(int width, int height)
{
    MIN_ZOOMED_IMG_SIZE.setWidth(width);
    MIN_ZOOMED_IMG_SIZE.setHeight(height);
    return this;
}

ImageWidget* ImageWidget::setPaintAreaOffset(int offset)
{
    this->PAINT_AREA_OFFEST = offset;
    return this;
}

ImageWidget* ImageWidget::setEnableSendLeftClickedPosInWidget(bool flag)
{
    enableSendLeftClickedPosInWidget = flag;
    return this;
}

ImageWidget* ImageWidget::setEnableSendLeftClickedPosInImage(bool flag)
{
    enableSendLeftClickedPosInImage = flag;
    return this;
}

QPoint ImageWidget::getDrawImageTopLeftPos() const { return paintImageRect.topLeft(); }

void ImageWidget::wheelEvent(QWheelEvent* e)
{
    if (!inputImg.isNull() && !enableOnlyShowImage && enableZoomImage) {
        int numDegrees = e->delta();
        if (numDegrees > 0) {
            imageZoomOut();
        } else {
            imageZoomIn();
        }
        updateZoomedImage();
        updateImageWidget();
    }
}

void ImageWidget::mousePressEvent(QMouseEvent* e)
{
    if (!inputImg.isNull() && !enableOnlyShowImage) {
        switch (e->button()) {
        case Qt::LeftButton:
            mouseStatus = Qt::LeftButton;
            mouseLeftKeyPressDownPos = e->pos();
            break;
        case Qt::RightButton:
            mouseStatus = Qt::RightButton;
            break;
        case Qt::MiddleButton:
            mouseStatus = Qt::MiddleButton;
            break;
        default:
            mouseStatus = Qt::NoButton;
        }
    }
}

void ImageWidget::mouseReleaseEvent(QMouseEvent* e)
{
    // 单击事件
    if (mouseStatus == Qt::LeftButton && !isImageDragging) {
        sendLeftClickedSignals(e);
        mouseStatus = Qt::NoButton;
    }
    if (!inputImg.isNull() && !enableOnlyShowImage && enableDragImage) {
        if (mouseStatus == Qt::LeftButton) {
            // 记录上次图像顶点
            paintImageLastTopLeft = paintImageRect.topLeft();
            // 释放后鼠标状态置No
            mouseStatus = Qt::NoButton;
            isImagePosChanged = true;
            isImageDragging = false;
            updateImageWidget();
        }
    }
}

void ImageWidget::fixDrawImageTopLeftPosOutterMode(const QRect& imageWidgetRect, QRect& qImgZoomedRect)
{
    // 图像在ImageWidget区域内则不用修正
    if (imageWidgetRect.intersects(qImgZoomedRect)) {
        return;
    }
    qDebug() << imageWidgetRect.topLeft() << imageWidgetRect.bottomRight();
    qDebug() << qImgZoomedRect.topLeft() << qImgZoomedRect.bottomRight();
    // 计算坐标偏差并修正
    if (qImgZoomedRect.right() < imageWidgetRect.left()) {
        qImgZoomedRect.moveRight(imageWidgetRect.left());
    }
    if (qImgZoomedRect.left() > imageWidgetRect.right()) {
        qImgZoomedRect.moveLeft(imageWidgetRect.right());
    }
    if (qImgZoomedRect.bottom() < imageWidgetRect.top()) {
        qImgZoomedRect.moveBottom(imageWidgetRect.top());
    }
    if (qImgZoomedRect.top() > imageWidgetRect.bottom()) {
        qImgZoomedRect.moveTop(imageWidgetRect.bottom());
    }
    qDebug() << "fixed: " << qImgZoomedRect.topLeft() << qImgZoomedRect.bottomRight();
}

void ImageWidget::fixDrawImageTopLeftPosInnerMode(const QRect& imageWidgetRect, QRect& paintImageRect)
{
    // 计算坐标偏差并修正
    qDebug() << imageWidgetRect.topLeft() << imageWidgetRect.bottomRight();
    qDebug() << paintImageRect.topLeft() << paintImageRect.bottomRight();
    if (paintImageRect.width() <= imageWidgetRect.width()) {
        if (paintImageRect.left() < imageWidgetRect.left()) {
            paintImageRect.moveLeft(imageWidgetRect.left());
        }
        if (paintImageRect.right() > imageWidgetRect.right()) {
            paintImageRect.moveRight(imageWidgetRect.right());
        }
    }
    if (paintImageRect.height() <= imageWidgetRect.height()) {
        if (paintImageRect.top() < imageWidgetRect.top()) {
            paintImageRect.moveTop(imageWidgetRect.top());
        }
        if (paintImageRect.bottom() > imageWidgetRect.bottom()) {
            paintImageRect.moveBottom(imageWidgetRect.bottom());
        }
    }
    if (paintImageRect.width() > imageWidgetRect.width()) {
        if (paintImageRect.left() > imageWidgetRect.left()) {
            paintImageRect.moveLeft(imageWidgetRect.left());
        }
        if (paintImageRect.right() < imageWidgetRect.right()) {
            paintImageRect.moveRight(imageWidgetRect.right());
        }
    }
    if (paintImageRect.height() > imageWidgetRect.height()) {
        if (paintImageRect.top() > imageWidgetRect.top()) {
            paintImageRect.moveTop(imageWidgetRect.top());
        }
        if (paintImageRect.bottom() < imageWidgetRect.bottom()) {
            paintImageRect.moveBottom(imageWidgetRect.bottom());
        }
    }
    qDebug() << "fixed: " << paintImageRect.topLeft() << paintImageRect.bottomRight() << paintImageRect.size();
}

void ImageWidget::mouseMoveEvent(QMouseEvent* e)
{
    // TODO: 在图像移动出Widget后，限定drawImageTopLeftPos，防止图像飞出Widget太远
    if (!inputImg.isNull() && !enableOnlyShowImage && enableDragImage) {
        if (mouseStatus == Qt::LeftButton) {
            // e->pos()为当前鼠标坐标 转换为相对移动距离
            paintImageRect.moveTopLeft(paintImageLastTopLeft + (e->pos() - mouseLeftKeyPressDownPos));
            isImageDragging = true;
            updateImageWidget();
        }
    }
}

void ImageWidget::paintEvent(QPaintEvent* e)
{
    QPainter painter(this);
    painter.setBrush(QBrush(QColor(200, 200, 200)));
    painter.drawRect(0, 0, this->width(), this->height());
    if (!inputImg.isNull()) {
        painter.drawImage(paintImageRect.topLeft(), paintImg);
    }
}

void ImageWidget::contextMenuEvent(QContextMenuEvent* e)
{
    if (!enableOnlyShowImage) {
        mMenu->exec(QCursor::pos());
        // 右键菜单弹出后 鼠标状态置No
        mouseStatus = Qt::NoButton;
    }
}

void ImageWidget::resizeEvent(QResizeEvent* e)
{
    imageWidgetPaintRect = QRect(-PAINT_AREA_OFFEST, -PAINT_AREA_OFFEST, this->width() + 2 * PAINT_AREA_OFFEST, this->height() + 2 * PAINT_AREA_OFFEST);
    if (!inputImg.isNull()) {
        // 如果图像没有被拖拽或者缩放过 置中缩放
        if (!isImagePosChanged && enableAutoFitWidget) {
            paintImg = inputImg.scaled(this->width() * zoomScale, this->height() * zoomScale, Qt::KeepAspectRatio);
            paintImageRect.moveTopLeft(getImageTopLeftPosWhenShowInCenter(paintImg, this));
            paintImageLastTopLeft = paintImageRect.topLeft();
        } else {
            // TODO: 拖动后的图像在ImageWidget尺寸发生变化后如何调整
        }
        if (isSelectMode)
            emit sendParentWidgetSizeChangedSignal();
        updateImageWidget();
        paintImageLastTopLeft = paintImageRect.topLeft();
    }
}

void ImageWidget::resetImageWidget()
{
    setImageAttributeWithAutoFitFlag(enableAutoFitWidget);
    isImagePosChanged = false;
    updateImageWidget();
}

void ImageWidget::imageZoomOut()
{
    // TODO: 使用scale和size共同限定
    if (zoomScale < MAX_ZOOM_SCALE) {
        zoomScale *= 1.1;
        isZoomedParametersChanged = true;
    }
}

void ImageWidget::imageZoomIn()
{
    // TODO: 使用scale和size共同限定
    if (zoomScale > MIN_ZOOM_SCALE) {
        zoomScale *= 1.0 / 1.1;
        isZoomedParametersChanged = true;
    }
}

void ImageWidget::createSelectRectInWidget()
{
    if (!inputImg.isNull()) {
        isSelectMode = true;
        SelectRect* m = new SelectRect(this);
        m->setGeometry(0, 0, this->geometry().width(), this->geometry().height());
        connect(m, SIGNAL(sendSelectModeExit()), this, SLOT(selectModeExit()));
        connect(this, SIGNAL(sendParentWidgetSizeChangedSignal()), m, SLOT(receiveParentSizeChangedSignal()));
        m->setImage(&inputImg, &paintImg, paintImageRect.topLeft());
        m->show();
    }
}

void ImageWidget::initializeContextmenu()
{
    mMenu = new QMenu(this);
    mMenuAdditionalFunction = new QMenu(mMenu);

    mActionResetParameters = mMenu->addAction(tr("重置")); // Reset
    mActionSave = mMenu->addAction(tr("另存为")); // Save As
    mActionSelect = mMenu->addAction(tr("截取")); // Crop
    mMenuAdditionalFunction = mMenu->addMenu(tr("更多功能")); // More Function
    mActionEnableDrag = mMenuAdditionalFunction->addAction(tr("启用拖拽")); // Enable Drag
    mActionEnableZoom = mMenuAdditionalFunction->addAction(tr("启用缩放")); // Enable Zoom
    mActionImageAutoFitWidget = mMenuAdditionalFunction->addAction(tr("启动自适应大小")); // Enable Image Fit Widget
    mActionLoadImageWithDefaultConfig
        = mMenuAdditionalFunction->addAction(tr("使用默认参数加载图片")); // Use Default Config to Load Image (such as Scale, Image Position)

    mActionEnableDrag->setCheckable(true);
    mActionEnableZoom->setCheckable(true);
    mActionImageAutoFitWidget->setCheckable(true);
    mActionLoadImageWithDefaultConfig->setCheckable(true);

    mActionEnableDrag->setChecked(enableDragImage);
    mActionEnableZoom->setChecked(enableZoomImage);
    mActionImageAutoFitWidget->setChecked(enableAutoFitWidget);
    mActionLoadImageWithDefaultConfig->setChecked(enableLoadImageWithDefaultConfig);

    connect(mActionResetParameters, SIGNAL(triggered()), this, SLOT(resetImageWidget()));
    connect(mActionSave, SIGNAL(triggered()), this, SLOT(save()));
    connect(mActionSelect, SIGNAL(triggered()), this, SLOT(createSelectRectInWidget()));
    connect(mActionEnableDrag, SIGNAL(toggled(bool)), this, SLOT(setEnableDrag(bool)));
    connect(mActionEnableZoom, SIGNAL(toggled(bool)), this, SLOT(setEnableZoom(bool)));
    connect(mActionImageAutoFitWidget, SIGNAL(toggled(bool)), this, SLOT(setEnableAutoFit(bool)));
    connect(mActionLoadImageWithDefaultConfig, SIGNAL(toggled(bool)), this, SLOT(setEnableLoadImageWithDefaultConfig(bool)));
}

void ImageWidget::sendLeftClickedSignals(QMouseEvent* e)
{
    if (enableSendLeftClickedPosInWidget)
        emit sendLeftClickedPosInWidgetSignal(e->x(), e->y());

    if (enableSendLeftClickedPosInImage) {
        if (inputImg.isNull()) {
            emit sendLeftClickedPosInImageSignal(-1, -1);
        } else {
            QPoint cursorPosInImage = getCursorPosInImage(inputImg, paintImg, paintImageRect.topLeft(), e->pos());
            // 如果光标不在图像上则返回-1
            if (cursorPosInImage.x() < 0 || cursorPosInImage.y() < 0 || cursorPosInImage.x() > inputImg.width() - 1
                || cursorPosInImage.y() > inputImg.height() - 1) {
                cursorPosInImage.setX(-1);
                cursorPosInImage.setY(-1);
            }
            emit sendLeftClickedPosInImageSignal(cursorPosInImage.x(), cursorPosInImage.y());
        }
    }
}

QPoint ImageWidget::getCursorPosInImage(const QImage& originalImage, const QImage& zoomedImage, const QPoint& imageLeftTopPos, const QPoint& cursorPos)
{
    // 计算当前光标在原始图像坐标系中相对于图像原点的位置
    QPoint resPoint;
    int distanceX = cursorPos.x() - imageLeftTopPos.x();
    int distanceY = cursorPos.y() - imageLeftTopPos.y();
    double xDivZoomedImageW = double(distanceX) / double(zoomedImage.width());
    double yDivZoomedImageH = double(distanceY) / double(zoomedImage.height());
    resPoint.setX(int(std::floor(originalImage.width() * xDivZoomedImageW)));
    resPoint.setY(int(std::floor(originalImage.height() * yDivZoomedImageH)));
    return resPoint;
}

void ImageWidget::save()
{
    if (!inputImg.isNull()) {
        QImage temp = inputImg.copy();
        QString filename
            = QFileDialog::getSaveFileName(this, tr("Open File"), QCoreApplication::applicationDirPath(), tr("Images (*.png *.xpm *.jpg *.tiff *.bmp)"));
        if (!filename.isEmpty() || !filename.isNull())
            temp.save(filename);
    }
}

void ImageWidget::selectModeExit() { isSelectMode = false; }

void ImageWidget::updateImageWidget()
{
    fixDrawImageTopLeftPosOutterMode(imageWidgetPaintRect, paintImageRect);
    update();
}

void ImageWidget::updateZoomedImage()
{
    // 图像为空直接返回
    if (inputImg.isNull())
        return;
    if (isZoomedParametersChanged) {
        lastPaintImgSize = paintImg.size();
    }
    // 减少拖动带来的QImage::scaled
    if (enableAutoFitWidget) {
        paintImg = inputImg.scaled(this->width() * zoomScale, this->height() * zoomScale, Qt::KeepAspectRatio);
    } else {
        paintImg = inputImg.scaled(inputImg.width() * zoomScale, inputImg.height() * zoomScale, Qt::KeepAspectRatio);
    }
    // TODO: 缩放过程中图像位置应不变
    if (isZoomedParametersChanged) {
        QSize zoomedImageChanged = lastPaintImgSize - paintImg.size();
        // 获取当前光标并计算出光标在图像中的位置
        QPoint cursorPosInWidget = this->mapFromGlobal(QCursor::pos());
        QPoint cursorPosInImage = getCursorPosInImage(inputImg, paintImg, paintImageRect.topLeft(), cursorPosInWidget);
        if (cursorPosInImage.x() < 0) {
            cursorPosInImage.setX(0);
        }
        if (cursorPosInImage.y() < 0) {
            cursorPosInImage.setY(0);
        }
        if (cursorPosInImage.x() > inputImg.width()) {
            cursorPosInImage.setX(inputImg.width() - 1);
        }
        if (cursorPosInImage.y() > inputImg.height()) {
            cursorPosInImage.setY(inputImg.height() - 1);
        }
        // 根据光标在图像的位置进行调整左上绘图点位置 保持鼠标悬停点为缩放中心点
        paintImageRect.moveTopLeft(paintImageLastTopLeft
            + QPoint(int(double(zoomedImageChanged.width()) * double(cursorPosInImage.x()) / double(inputImg.width() - 1)),
                int(double(zoomedImageChanged.height()) * double(cursorPosInImage.y()) / double(inputImg.height() - 1))));
        paintImageRect.setSize(paintImg.size());

        if (paintImageRect.topLeft() != paintImageLastTopLeft) {
            isImagePosChanged = true;
        }
        paintImageLastTopLeft = paintImageRect.topLeft();
    }
    isZoomedParametersChanged = false;
}

void ImageWidget::setDefaultParameters()
{
    zoomScale = 1.0;
    if (!inputImg.isNull()) {
        // 首先恢复zoomedImage大小再调整drawPos
        updateZoomedImage();
        paintImageRect.moveTopLeft(getImageTopLeftPosWhenShowInCenter(paintImg, this));
        paintImageLastTopLeft = paintImageRect.topLeft();
    }
}

QPoint ImageWidget::getImageTopLeftPosWhenShowInCenter(const QImage& img, const QWidget* iw)
{
    QPoint resPoint(0, 0);
    if (img.width() < iw->width()) {
        resPoint.setX((iw->width() - img.width()) / 2);
    }
    if (img.height() < iw->height()) {
        resPoint.setY((iw->height() - img.height()) / 2);
    }
    return resPoint;
}
