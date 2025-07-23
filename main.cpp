#include <QApplication>
#include <QMainWindow>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QToolBar>
#include <QLineEdit>
#include <QStatusBar>
#include <QMouseEvent>
#include <QSvgGenerator>
#include <QPainter>
#include <QFileDialog>
#include <QMenuBar>
#include <QVBoxLayout>
#include <QLabel>

enum Tool { None, Rectangle, Ellipse, Line, Bezier };

class GridItem : public QGraphicsItem {
public:
    GridItem(qreal step = 20, QSize size = QSize(2000, 2000)) : step(step), size(size) {}
    QRectF boundingRect() const override { return QRectF(0, 0, size.width(), size.height()); }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setPen(QPen(Qt::lightGray, 0));
        for (qreal x = 0; x <= size.width(); x += step)
            p->drawLine(QPointF(x, 0), QPointF(x, size.height()));
        for (qreal y = 0; y <= size.height(); y += step)
            p->drawLine(QPointF(0, y), QPointF(size.width(), y));
    }
private:
    qreal step;
    QSize size;
};

class RulerItem : public QGraphicsItem {
public:
    RulerItem(bool horizontal, int length = 2000) : horizontal(horizontal), length(length) {}
    QRectF boundingRect() const override { return horizontal ? QRectF(0, 0, length, 20) : QRectF(0, 0, 20, length); }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->fillRect(boundingRect(), Qt::lightGray);
        p->setPen(Qt::black);
        for (int i = 0; i < length; i += 10) {
            if (horizontal)
                p->drawLine(i, 20, i, i % 50 == 0 ? 0 : 10);
            else
                p->drawLine(20, i, i % 50 == 0 ? 0 : 10, i);
        }
    }
private:
    bool horizontal;
    int length;
};

class Canvas : public QGraphicsView {
    Q_OBJECT

public:
    Canvas(QWidget *parent = nullptr) : QGraphicsView(parent), tool(None), snapToGrid(true), drawing(false), currentItem(nullptr), circlePending(false), squarePending(false) {
        scene = new QGraphicsScene(0, 0, 2000, 2000, this);
        setScene(scene);
        setRenderHint(QPainter::Antialiasing);
        grid = new GridItem(20, QSize(2000, 2000));
        scene->addItem(grid);
        scene->addItem(new RulerItem(true));
        scene->addItem(new RulerItem(false));
        currentPos = QPointF(100, 100);

        snapIndicator = scene->addEllipse(-4, -4, 8, 8, QPen(Qt::gray), QBrush(QColor(100, 100, 100, 100)));
        snapIndicator->setZValue(999);
        snapIndicator->setVisible(true);

    }

    void setTool(Tool t) { tool = t; }
    void toggleSnap(bool enable) { snapToGrid = enable; }

    void exportToSvg(const QString &filename) {
        QSvgGenerator generator;
        generator.setFileName(filename);
        generator.setSize(scene->sceneRect().size().toSize());
        generator.setViewBox(scene->sceneRect());
        generator.setTitle("SVG Drawing");
        generator.setDescription("Exported drawing");
        QPainter painter(&generator);
        scene->render(&painter);
    }

    QPointF snap(const QPointF &pt) {
        if (!snapToGrid) return pt;
        return QPointF(qRound(pt.x() / 20) * 20, qRound(pt.y() / 20) * 20);
    }

signals:
    void mouseMoved(QPointF pos);

protected:
    void mouseMoveEvent(QMouseEvent *e) override {
        QPointF pt = snap(mapToScene(e->pos()));
        emit mouseMoved(pt);
        if (!drawing || !currentItem || tool == Bezier) return;
        switch (tool) {
            case Rectangle:
                static_cast<QGraphicsRectItem*>(currentItem)->setRect(QRectF(start, pt).normalized());
                break;
            case Ellipse: {
                qreal r = QLineF(start, pt).length();
                static_cast<QGraphicsEllipseItem*>(currentItem)->setRect(start.x() - r, start.y() - r, 2 * r, 2 * r);
                break;
            }
            case Line:
                static_cast<QGraphicsLineItem*>(currentItem)->setLine(QLineF(start, pt));
                break;
            default: break;
        }
        //QPointF pt = snap(mapToScene(e->pos()));
        emit mouseMoved(pt);
        snapIndicator->setRect(pt.x() - 4, pt.y() - 4, 8, 8);

    }

    void mousePressEvent(QMouseEvent *e) override {
        if (tool == None) return;
        drawing = true;
        start = snap(mapToScene(e->pos()));
        switch (tool) {
            case Rectangle:
                currentItem = scene->addRect(QRectF(start, start), QPen(Qt::black));
                break;
            case Ellipse:
                currentItem = scene->addEllipse(0, 0, 0, 0, QPen(Qt::blue));
                break;
            case Line:
                currentItem = scene->addLine(QLineF(start, start), QPen(Qt::red));
                break;
            case Bezier:
                bezierPts << start;
                if (bezierPts.size() == 4) {
                    QPainterPath path;
                    path.moveTo(bezierPts[0]);
                    path.cubicTo(bezierPts[1], bezierPts[2], bezierPts[3]);
                    scene->addPath(path, QPen(Qt::darkGreen));
                    bezierPts.clear();
                    drawing = false;
                }
                break;
            default: break;
        }
    }

    void mouseReleaseEvent(QMouseEvent *) override {
        if (tool != Bezier) {
            drawing = false;
            currentItem = nullptr;
        }
    }

public slots:
    void runCommand(const QString &cmd) {
        QStringList tokens = cmd.toLower().split(QRegExp("\\s+"));
        if (tokens.isEmpty()) return;

        static double lastRadius = 40.0;
        static double lastSize = 80.0;

        auto drawMarker = [&]() {
            qreal r = 2.5;
            scene->addEllipse(currentPos.x() - r, currentPos.y() - r, 2 * r, 2 * r, QPen(Qt::black), QBrush(Qt::black))->setZValue(1000);
        };

        if (tokens[0] == "start" && tokens.size() >= 3) {
            bool ok1, ok2;
            double x = tokens[1].remove('x').toDouble(&ok1);
            double y = tokens[2].remove('y').toDouble(&ok2);
            if (ok1 && ok2) {
                currentPos = QPointF(x, y);
                circlePending = false;
                squarePending = false;
                drawMarker();
            }
        }
        else if (tokens[0] == "move" && tokens.size() >= 3) {
            bool ok1, ok2;
            double dx = tokens[1].toDouble(&ok1);
            double dy = tokens[2].toDouble(&ok2);
            if (ok1 && ok2) {
                QPointF delta(dx, dy);
                if (circlePending) {
                    double r = QLineF(pendingCircleCenter, pendingCircleCenter + delta).length();
                    scene->addEllipse(pendingCircleCenter.x() - r, pendingCircleCenter.y() - r, 2 * r, 2 * r, QPen(Qt::blue));
                    lastRadius = r;
                    currentPos = pendingCircleCenter + delta;
                    circlePending = false;
                    drawMarker();
                } else if (squarePending) {
                    double size = QLineF(pendingSquareCenter, pendingSquareCenter + delta).length() * 2;
                    scene->addRect(pendingSquareCenter.x() - size / 2, pendingSquareCenter.y() - size / 2, size, size, QPen(Qt::black));
                    lastSize = size;
                    currentPos = pendingSquareCenter + delta;
                    squarePending = false;
                    drawMarker();
                } else {
                    currentPos += delta;
                    drawMarker();
                }
            }
        }
        else if (tokens[0] == "bezier" && tokens.size() == 5) {
            QVector<QPointF> pts;
            for (int i = 1; i <= 4; ++i) {
                QStringList xy = tokens[i].split(',');
                if (xy.size() != 2) return;
                bool ok1, ok2;
                double x = xy[0].toDouble(&ok1);
                double y = xy[1].toDouble(&ok2);
                if (!ok1 || !ok2) return;
                pts << QPointF(x, y);
            }
            QPainterPath path;
            path.moveTo(pts[0]);
            path.cubicTo(pts[1], pts[2], pts[3]);
            scene->addPath(path, QPen(Qt::darkGreen));
        }

        else if (tokens[0] == "circle") {
            if (tokens.size() == 1) {
                pendingCircleCenter = currentPos;
                circlePending = true;
            } else {
                bool ok;
                double r = tokens[1].toDouble(&ok);
                if (ok) {
                    scene->addEllipse(currentPos.x() - r, currentPos.y() - r, 2 * r, 2 * r, QPen(Qt::blue));
                    lastRadius = r;
                    drawMarker();
                }
            }
        }
        else if (tokens[0] == "square") {
            if (tokens.size() == 1) {
                pendingSquareCenter = currentPos;
                squarePending = true;
            } else {
                bool ok;
                double s = tokens[1].toDouble(&ok);
                if (ok) {
                    scene->addRect(currentPos.x() - s / 2, currentPos.y() - s / 2, s, s, QPen(Qt::black));
                    lastSize = s;
                    drawMarker();
                }
            }
        }
        else if (tokens[0] == "line" && tokens.size() >= 3) {
            bool ok1, ok2;
            double dx = tokens[1].toDouble(&ok1);
            double dy = tokens[2].toDouble(&ok2);
            if (ok1 && ok2) {
                QPointF end = currentPos + QPointF(dx, dy);
                scene->addLine(QLineF(currentPos, end), QPen(Qt::red));
                currentPos = end;
                drawMarker();
            }
        }
        else if (tokens[0] == "ellipse" && tokens.size() >= 3) {
            bool ok1, ok2;
            double rx = tokens[1].toDouble(&ok1);
            double ry = tokens[2].toDouble(&ok2);
            if (ok1 && ok2) {
                scene->addEllipse(currentPos.x() - rx, currentPos.y() - ry, 2 * rx, 2 * ry, QPen(Qt::darkGreen));
                drawMarker();
            }
        }
    }

private:
    QGraphicsScene *scene;
    Tool tool;
    bool snapToGrid;
    bool drawing;
    QPointF start;
    QPointF currentPos;
    QGraphicsItem *currentItem;
    GridItem *grid;
    QVector<QPointF> bezierPts;

    QPointF pendingCircleCenter;
    QPointF pendingSquareCenter;
    bool circlePending;
    bool squarePending;
    QGraphicsEllipseItem *snapIndicator;

};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow() {
        canvas = new Canvas(this);
        cmdInput = new QLineEdit(this);
        QLabel *status = new QLabel("Ready");
        statusBar()->addWidget(status);

        connect(canvas, &Canvas::mouseMoved, this, [=](QPointF pt) {
            status->setText(QString("X: %1  Y: %2").arg(pt.x()).arg(pt.y()));
        });

        connect(cmdInput, &QLineEdit::returnPressed, this, [=]() {
            canvas->runCommand(cmdInput->text());
            cmdInput->clear();
        });

        QToolBar *toolbar = addToolBar("Tools");
        toolbar->addAction("Rect", this, [=]() { canvas->setTool(Rectangle); });
        toolbar->addAction("Ellipse", this, [=]() { canvas->setTool(Ellipse); });
        toolbar->addAction("Line", this, [=]() { canvas->setTool(Line); });
        toolbar->addAction("Bezier", this, [=]() { canvas->setTool(Bezier); });
        QAction *snap = toolbar->addAction("Snap On");
        snap->setCheckable(true); snap->setChecked(true);
        connect(snap, &QAction::toggled, canvas, &Canvas::toggleSnap);

        QMenu *fileMenu = menuBar()->addMenu("File");
        fileMenu->addAction("Export SVG", this, [=]() {
            QString fn = QFileDialog::getSaveFileName(this, "Save SVG", "", "*.svg");
            if (!fn.isEmpty()) canvas->exportToSvg(fn);
        });

        QWidget *central = new QWidget;
        QVBoxLayout *layout = new QVBoxLayout(central);
        layout->addWidget(canvas);
        layout->addWidget(cmdInput);
        layout->setMargin(0);
        setCentralWidget(central);
        resize(900, 700);
        setWindowTitle("SVG Drawing App");
    }

private:
    Canvas *canvas;
    QLineEdit *cmdInput;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow win;
    win.show();
    return app.exec();
}
