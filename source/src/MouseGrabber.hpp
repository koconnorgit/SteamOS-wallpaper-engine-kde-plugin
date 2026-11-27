#pragma once
#include <QQuickItem>
#include <QEvent>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QPointer>

namespace wekde
{

class MouseGrabber : public QQuickItem
{
    Q_OBJECT
	Q_PROPERTY(QQuickItem* target READ target WRITE setTarget NOTIFY targetChanged)

public:
	MouseGrabber(QQuickItem *parent = nullptr);
	virtual ~MouseGrabber() override {};

	QQuickItem* target() const; 

	void setTarget(QQuickItem*);

protected:
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void hoverMoveEvent(QHoverEvent *) override;

signals:
	void targetChanged();

private:
    void sendMouseEvent(QMouseEvent*);
    void sendHoverEvent(QHoverEvent*);
    QPointer<QQuickItem> m_target {nullptr};
};
}
