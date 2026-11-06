#include "mainwindow.h"

#include <algorithm>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "install.h"
#include "keybutton.h"
#include "setuppage.h"
#include "paths.h"

namespace {

// the artwork across the top, filling its width and cut from the top down, fading into the window below
class Banner : public QWidget {
public:
    // the logo's height in the image, with a little sky above and below: a banner shorter than it shows the picture
    // smaller, so the logo stays whole
    static constexpr double kLogoRows = 420;

    explicit Banner(QWidget* parent) : QWidget(parent), image_(":/banner.jpg") {
        setFixedHeight(300);  // MainWindow::FitBanner makes it shorter in a short window
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override { return {960, 300}; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), palette().color(QPalette::Window));
        const double cover = std::max(double(width()) / image_.width(), double(height()) / image_.height());
        const double scale = std::min(cover, height() / kLogoRows);
        if (scale < cover) {
            // the scene below the logo faintly across the whole width, behind the smaller picture
            const QRectF scene(0, kLogoRows, image_.width(), image_.height() - kLogoRows);
            const double fill = std::max(double(width()) / scene.width(), double(height()) / scene.height());
            const QSizeF back(scene.width() * fill, scene.height() * fill);
            p.setOpacity(0.35);
            p.drawPixmap(QRectF((width() - back.width()) / 2.0, 0.0, back.width(), back.height()), image_, scene);
            p.setOpacity(1.0);
        }
        const QSizeF size(image_.width() * scale, image_.height() * scale);
        // centred across, from the top edge (the logo) down
        const QRectF target((width() - size.width()) / 2.0, 0.0, size.width(), size.height());
        p.drawPixmap(target, image_, image_.rect());
        if (scale < cover) {
            // its sides fade into the faint one
            QColor edge = palette().color(QPalette::Window);
            edge.setAlpha(0);
            QColor solid = edge;
            solid.setAlpha(150);
            const double fadeW = std::min(80.0, size.width() / 4);
            for (const bool left : {true, false}) {
                const double x = left ? target.left() : target.right() - fadeW;
                QLinearGradient g(x, 0, x + fadeW, 0);
                g.setColorAt(left ? 0 : 1, solid);
                g.setColorAt(left ? 1 : 0, edge);
                p.fillRect(QRectF(x, 0, fadeW, height()), g);
            }
        }
        QLinearGradient fade(0, height() * 0.72, 0, height());
        QColor base = palette().color(QPalette::Window);
        base.setAlpha(0);
        fade.setColorAt(0, base);
        base.setAlpha(255);
        fade.setColorAt(1, base);
        p.fillRect(rect(), fade);
    }

private:
    QPixmap image_;
};

QLabel* HintLabel(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    l->setFont(f);
    QPalette pal = l->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
    l->setPalette(pal);
    return l;
}

QScrollArea* Scroll(QWidget* page) {
    auto* s = new QScrollArea;
    s->setWidget(page);
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    return s;
}

QGroupBox* Group(const QString& title, QFormLayout** form) {
    auto* g = new QGroupBox(title);
    *form = new QFormLayout(g);
    (*form)->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    (*form)->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return g;
}

// a slider with its number beside it; the spin box holds the value
QSpinBox* SliderRow(QFormLayout* form, const QString& label, int min, int max, const QString& suffix) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    auto* spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setSuffix(suffix);
    spin->setMinimumWidth(90);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    h->addWidget(slider, 1);
    h->addWidget(spin);
    form->addRow(label, row);
    return spin;
}

// a string choice that keeps a value from settings.toml the list doesn't have
void SetChoice(QComboBox* c, const QString& value, const QString& emptyText = {}) {
    if (value.isEmpty() && !emptyText.isEmpty()) {
        c->setCurrentIndex(0);
        return;
    }
    int i = c->findData(value);
    if (i < 0) {
        c->addItem(value, value);
        i = c->count() - 1;
    }
    c->setCurrentIndex(i);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Conker: Recomped & Reloaded"));
    const QPixmap art(":/banner.jpg");
    setWindowIcon(QIcon(art.copy(QRect(880, 385, 380, 380))));  // Conker in his helmet

    auto* central = new QWidget;
    auto* v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    banner_ = new Banner(central);
    v->addWidget(banner_);

    auto* body = new QWidget;
    auto* bv = new QVBoxLayout(body);
    bv->setContentsMargins(16, 4, 16, 14);
    tabs_ = new QTabWidget;
    tabs_->addTab(Scroll(BuildGraphics()), tr("Graphics"));
    tabs_->addTab(Scroll(BuildEnhancements()), tr("Enhancements"));
    tabs_->addTab(Scroll(BuildAudio()), tr("Audio"));
    tabs_->addTab(Scroll(BuildControls()), tr("Controls"));
    tabs_->addTab(Scroll(BuildMultiplayer()), tr("Multiplayer"));
    tabs_->addTab(Scroll(BuildMods()), tr("Mods"));
    tabs_->addTab(Scroll(BuildLauncher()), tr("Launcher"));
    bv->addWidget(tabs_, 1);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 10, 0, 0);
    status_ = new QLabel;
    status_->setWordWrap(true);
    auto* defaults = new QPushButton(tr("Restore defaults…"));
    connect(defaults, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Restore defaults"),
                                  tr("Reset every graphics, enhancement, audio and multiplayer setting to its default? "
                                     "Controls are kept.")) != QMessageBox::Yes)
            return;
        Config empty;
        loading_ = true;
        for (const Bind& b : binds_) b.load(empty);
        loading_ = false;
        SetStatus(tr("Defaults restored. They are saved when you press Save or Play."));
    });
    auto* save = new QPushButton(tr("Save"));
    connect(save, &QPushButton::clicked, this, [this] {
        if (SaveAll()) SetStatus(tr("Saved to %1").arg(config_.File()));
    });
    play_ = new QPushButton(tr("PLAY"));
    play_->setDefault(true);
    play_->setObjectName("primary");  // Start on a controller presses it (gamepad.h)
    play_->setMinimumSize(160, 44);
    QFont pf = play_->font();
    pf.setBold(true);
    pf.setPointSizeF(pf.pointSizeF() * 1.35);
    play_->setFont(pf);
    play_->setStyleSheet(
        "QPushButton { background: #e8681a; color: white; border: none; border-radius: 6px; padding: 6px 24px; }"
        "QPushButton:hover { background: #f57c2c; }"
        "QPushButton:pressed { background: #c85510; }"
        "QPushButton:disabled { background: #7a5a46; color: #ddd; }");
    connect(play_, &QPushButton::clicked, this, &MainWindow::Play);
    bar->addWidget(status_, 1);
    bar->addWidget(defaults);
    bar->addWidget(save);
    bar->addSpacing(8);
    bar->addWidget(play_);
    bv->addLayout(bar);
    stack_ = new QStackedWidget;
    stack_->addWidget(body);
    setupPage_ = new SetupPage;
    stack_->addWidget(setupPage_);
    v->addWidget(stack_, 1);
    setCentralWidget(central);
    // no taller or wider than the screen: a Steam Deck's is 1280 × 800, and Play is at the bottom
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    const int height = std::min(860, screen.height());
    FitBanner(height);  // first, or the full banner makes the window too tall to fit
    resize(std::min(1000, screen.width()), height);
    connect(setupPage_, &SetupPage::Done, this, [this] {
        stack_->setCurrentIndex(0);
        LoadAll();
        SetStatus(tr("The game is ready. Press Play!"));
    });
    connect(setupPage_, &SetupPage::Back, this, [this] { stack_->setCurrentIndex(0); });

    if (install::GameBuilt()) install::MakePortable();  // its settings and saves stay in game/
    LoadAll();
    if (install::GameBuilt()) play_->setFocus();  // a controller starts on Play
    if (!install::GameBuilt()) ShowSetup(false);
    // a launcher update that brings new game code: the game is rebuilt to get it (asked once a start, not with --play)
    else if (install::KitChanged() && !QApplication::arguments().contains("--play") &&
             !QApplication::arguments().contains("--screenshot"))
        QTimer::singleShot(0, this, [this] {
            const auto answer = QMessageBox::question(
                this, tr("Game update"),
                tr("This launcher comes with an updated game. Rebuild the game now to get it?\n\n"
                   "It takes a few minutes and works offline. Your settings and saves are kept."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (answer == QMessageBox::Yes) ShowSetup(true);
            else SetStatus(tr("An updated game is ready to build: Launcher tab → Rebuild the game."));
        });
    // the decensor mod's latest release, in the background (the mods folder is known only now)
    if (decensorAuto_->isChecked() && !modsPath_->text().isEmpty()) {
        decensorNote_ = tr("Checking GitHub…");
        RefreshDecensor();
        QTimer::singleShot(0, decensor_, &Decensor::Check);
    }
}

// ── simple bound controls ───────────────────────────────────────────────────────────────────────────────────────────

void MainWindow::Hint(QFormLayout* form, const QString& text) { form->addRow(QString(), HintLabel(text)); }

QCheckBox* MainWindow::Check(QFormLayout* form, const QString& label, const QString& key, bool def,
                             const QString& hint) {
    auto* c = new QCheckBox(label);
    form->addRow(c);
    if (!hint.isEmpty()) Hint(form, hint);
    binds_.push_back({[=](const Config& cfg) { c->setChecked(cfg.Bool(key, def)); },
                      [=](Config& cfg) { cfg.SetBool(key, c->isChecked()); }});
    return c;
}

QSpinBox* MainWindow::Percent(QFormLayout* form, const QString& label, const QString& key, int min, int max, int def,
                              const QString& suffix) {
    QSpinBox* spin = SliderRow(form, label, min, max, suffix);
    binds_.push_back({[=](const Config& cfg) { spin->setValue(std::clamp(cfg.Int(key, def), min, max)); },
                      [=](Config& cfg) { cfg.SetInt(key, spin->value()); }});
    return spin;
}

QComboBox* MainWindow::IntChoice(QFormLayout* form, const QString& label, const QString& key,
                                 const QList<int>& values, const QStringList& texts, int def) {
    auto* c = new QComboBox;
    for (int i = 0; i < values.size(); i++) c->addItem(texts[i], values[i]);
    form->addRow(label, c);
    binds_.push_back({[=](const Config& cfg) {
                          const int i = c->findData(cfg.Int(key, def));
                          c->setCurrentIndex(i >= 0 ? i : std::max(0, c->findData(def)));
                      },
                      [=](Config& cfg) { cfg.SetInt(key, c->currentData().toInt()); }});
    return c;
}

// ── pages ───────────────────────────────────────────────────────────────────────────────────────────────────────────

QWidget* MainWindow::BuildGraphics() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    QFormLayout* f;

    v->addWidget(Group(tr("Display"), &f));
    {
        auto* mode = new QComboBox;
        mode->addItem(tr("Fullscreen"), "fullscreen");
        mode->addItem(tr("Borderless"), "borderless");
        mode->addItem(tr("Windowed"), "windowed");
        f->addRow(tr("Mode"), mode);
        binds_.push_back({[=](const Config& c) { SetChoice(mode, c.Str("display_mode", "borderless")); },
                          [=](Config& c) { c.SetStr("display_mode", mode->currentData().toString()); }});
    }
    {
        resolution_ = new QComboBox;
        resolution_->addItem(tr("Desktop"), QSize(0, 0));
        const QSize desk = QGuiApplication::primaryScreen()->size() * QGuiApplication::primaryScreen()->devicePixelRatio();
        const QList<QSize> common = {{3840, 2160}, {3440, 1440}, {2560, 1600}, {2560, 1440}, {2560, 1080},
                                     {1920, 1200}, {1920, 1080}, {1680, 1050}, {1600, 900},  {1440, 900},
                                     {1366, 768},  {1280, 800},  {1280, 720}};
        for (const QSize& s : common)
            if (s.width() <= desk.width() && s.height() <= desk.height())
                resolution_->addItem(QString("%1 × %2").arg(s.width()).arg(s.height()), s);
        f->addRow(tr("Resolution"), resolution_);
        binds_.push_back({[this](const Config& c) {
                              const QSize s(c.Int("width", 0), c.Int("height", 0));
                              int i = resolution_->findData(s);
                              if (i < 0) {
                                  resolution_->addItem(QString("%1 × %2").arg(s.width()).arg(s.height()), s);
                                  i = resolution_->count() - 1;
                              }
                              resolution_->setCurrentIndex(i);
                          },
                          [this](Config& c) {
                              const QSize s = resolution_->currentData().toSize();
                              c.SetInt("width", s.width());
                              c.SetInt("height", s.height());
                          }});
    }
    {
        aspect_ = new QComboBox;
        aspect_->addItem(tr("Automatic (the screen's shape)"), "auto");
        for (const char* a : {"4:3", "16:10", "16:9", "21:9", "32:9"}) aspect_->addItem(a, a);
        f->addRow(tr("Aspect ratio"), aspect_);
        Hint(f, tr("Wider than 4:3 shows more of the scene, not a stretched picture."));
        binds_.push_back({[this](const Config& c) { SetChoice(aspect_, c.Str("aspect", "auto")); },
                          [this](Config& c) { c.SetStr("aspect", aspect_->currentData().toString()); }});
    }
    {
        gpu_ = new QComboBox;
        gpu_->addItem(tr("Automatic (a dedicated GPU first)"), QString());
        f->addRow(tr("Graphics adapter"), gpu_);
        binds_.push_back({[this](const Config& c) { SetChoice(gpu_, c.Str("gpu"), "auto"); },
                          [this](Config& c) { c.SetStr("gpu", gpu_->currentData().toString()); }});
    }

    v->addWidget(Group(tr("Quality"), &f));
    Percent(f, tr("Render resolution"), "render_percent", 25, 400, 100);
    Hint(f, tr("Percent of the output resolution the 3D scene is drawn at. Above 100% supersamples."));
    IntChoice(f, tr("Frame rate"), "frame_rate", {30, 60, 120, 180, 240},
              {tr("30 (original)"), "60", "120", "180", "240"}, 60);
    Check(f, tr("Game logic at 30 (original timing)"), "console_timing", false,
          tr("Runs the game itself at 30 like the original and draws the frames above that in between. Avoids "
             "60 fps bugs such as the barn roof death in Barn Boys, but controls respond at 30. Shows at most 120."));
    Check(f, tr("V-Sync"), "vsync", true);
    IntChoice(f, tr("Anisotropic filtering"), "anisotropic_filtering", {0, 2, 4, 8, 16},
              {tr("Game default"), "2×", "4×", "8×", "16×"}, 0);
    IntChoice(f, tr("MSAA"), "msaa", {1, 2, 4, 8}, {tr("Off"), "2×", "4×", "8×"}, 1);

    v->addWidget(Group(tr("Post-processing"), &f));
    Check(f, tr("Vignette"), "vignette", true,
          tr("The game's darkened screen edges. The red flash when you're hurt stays either way."));
    Check(f, tr("SMAA anti-aliasing"), "smaa", false);
    Percent(f, tr("Film grain"), "film_grain", 0, 100, 0);
    Percent(f, tr("Chromatic aberration"), "chromatic_aberration", 0, 100, 0);
    QSpinBox* ao = Percent(f, tr("Ambient occlusion"), "ambient_occlusion", 0, 100, 0);
    QSpinBox* reach = Percent(f, tr("Ambient occlusion reach"), "ambient_occlusion_radius", 5, 200, 30, QString());
    auto aoReach = [=](int value) { reach->parentWidget()->setEnabled(value > 0); };
    connect(ao, &QSpinBox::valueChanged, this, aoReach);
    aoReach(0);

    v->addWidget(Group(tr("Overlay and startup"), &f));
    Check(f, tr("FPS counter in the corner"), "fps_overlay", false);
    Check(f, tr("FPS in the window title"), "fps_in_title", true);
    Check(f, tr("Prepare shaders while the game starts"), "shader_warmup", true,
          tr("Fewer stutters the first time effects appear, for a slightly longer start."));
    v->addStretch(1);
    return page;
}

QWidget* MainWindow::BuildEnhancements() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    QFormLayout* f;
    v->addWidget(Group(tr("Lighting"), &f));
    Check(f, tr("Per-pixel lighting"), "per_pixel_lighting", true, tr("Smoother light and shadows on characters and objects."));
    v->addWidget(Group(tr("Fur"), &f));
    Check(f, tr("Fur shading"), "fur_shading", false, tr("Softer, more natural-looking fur."));
    Check(f, tr("Full distant fur"), "fur_distant", true, tr("Fur stays thick far away instead of thinning out."));
    v->addWidget(Group(tr("Characters"), &f));
    Check(f, tr("Force max character LoD"), "character_max_lod", true,
          tr("Players and bots keep their full model and smooth animation however far away."));
    v->addWidget(Group(tr("Grass"), &f));
    Check(f, tr("Wind on distant grass"), "grass_wind", true, tr("Faraway grass sways in the wind too."));
    Check(f, tr("Force max grass LoD"), "grass_max_lod", true,
          tr("Faraway grass stays as thick as nearby grass instead of thinning out."));
    Check(f, tr("Fade distant grass"), "grass_fade", false,
          tr("Faraway grass fades away as on the console. Off: it stays solid, however far."));
    v->addWidget(Group(tr("Water and reflections"), &f));
    Check(f, tr("Dynamic water"), "dynamic_water", false, tr("Water ripples and makes waves as things move through it."));
    Check(f, tr("Floor depth of field"), "floor_depth", true, tr("Fixes the blur on shiny, reflective floors."));
    v->addWidget(Group(tr("Gameplay"), &f));
    Check(f, tr("Skippable cutscenes"), "skippable_cutscenes", false,
          tr("Press LT to skip any cutscene, even ones you haven't seen."));
    Check(f, tr("Rope physics"), "rope_physics", true, tr("Ropes swing realistically when Conker hangs on them."));
    Check(f, tr("Overpowered Frying Pan"), "overpowered_pan", false,
          tr("Conker swings the frying pan instead of the baseball bat, with 10x melee damage."));
    v->addStretch(1);
    return page;
}

QWidget* MainWindow::BuildAudio() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    QFormLayout* f;
    v->addWidget(Group(tr("Language"), &f));
    {
        // the console's language setting (live-and-recompiled app::Language), which the game reads as it starts
        auto* language = new QComboBox;
        language->addItem("English", "english");
        language->addItem("Français", "french");
        language->addItem("Deutsch", "german");
        language->addItem("Español", "spanish");
        language->addItem("Italiano", "italian");
        language->addItem("日本語", "japanese");
        f->addRow(tr("Language"), language);
        Hint(f, tr("Menus and subtitles. The voices are English in every language. The decensor mod's subtitles "
                   "cover English, French, Italian and Spanish."));
        binds_.push_back({[=](const Config& c) { SetChoice(language, c.Str("language", "english")); },
                          [=](Config& c) { c.SetStr("language", language->currentData().toString()); }});
    }
    v->addWidget(Group(tr("Sound"), &f));
    Percent(f, tr("Volume"), "volume", 0, 100, 100);
    Percent(f, tr("Cutscene volume"), "cutscene_volume", 0, 12, 0, tr(" dB"))->setPrefix("+");
    Hint(f, tr("Cutscenes play quieter than the rest of the game: this raises them while one plays."));
    audioDevice_ = new QComboBox;
    audioDevice_->addItem(tr("System default"), QString());
    f->addRow(tr("Output device"), audioDevice_);
    Hint(f, tr("The game follows your system's default output. A device picked in an older version is kept here."));
    binds_.push_back({[this](const Config& c) { SetChoice(audioDevice_, c.Str("audio_device"), "default"); },
                      [this](Config& c) { c.SetStr("audio_device", audioDevice_->currentData().toString()); }});
    {
        // the output's channels (live-and-recompiled audio::ChannelsFor): auto takes 5.1 when the device has six
        auto* speakers = new QComboBox;
        speakers->addItem(tr("Automatic"), "auto");
        speakers->addItem(tr("Stereo"), "stereo");
        speakers->addItem(tr("5.1 surround"), "5.1");
        f->addRow(tr("Speakers"), speakers);
        Hint(f, tr("Automatic plays 5.1 when the output device is set up for six or more channels, and stereo "
                   "otherwise. On 5.1, 3D sounds are placed front to back as well. Headphones: stereo, unless your "
                   "sound card or software turns 5.1 into virtual surround."));
        binds_.push_back({[=](const Config& c) { SetChoice(speakers, c.Str("speakers", "auto")); },
                          [=](Config& c) { c.SetStr("speakers", speakers->currentData().toString()); }});
    }
    Check(f, tr("Mute when the game is in the background"), "mute_in_background", false);
    v->addStretch(1);
    return page;
}

QWidget* MainWindow::BuildControls() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    QFormLayout* f;

    v->addWidget(Group(tr("Player"), &f));
    player_ = new QComboBox;
    for (int p = 1; p <= 4; p++) player_->addItem(tr("Player %1").arg(p));
    auto* playerRow = new QWidget;
    auto* ph = new QHBoxLayout(playerRow);
    ph->setContentsMargins(0, 0, 0, 0);
    ph->addWidget(player_, 1);
    auto* playerInfo = new QPushButton(tr("ℹ️ 3–4 Players / LAN Info…"));
    playerInfo->setToolTip(tr("Information on split-screen player limits and 3–4 player LAN play"));
    connect(playerInfo, &QPushButton::clicked, this, &MainWindow::ShowMultiplayerInfoDialog);
    ph->addWidget(playerInfo);
    f->addRow(tr("Settings for"), playerRow);
    Hint(f, tr("Local split screen supports up to 2 players on one PC. To play with 3–4 players (2v2), use Online Play / LAN. Click the info button above for details."));
    Check(f, tr("Give new controllers to the next free player"), "auto_controllers", true);
    useKeyboard_ = new QCheckBox(tr("Keyboard"));
    useMouse_ = new QCheckBox(tr("Mouse"));
    {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(useKeyboard_);
        h->addWidget(useMouse_);
        h->addStretch(1);
        f->addRow(tr("Plays with"), row);
    }
    controller_ = new QComboBox;
    controller_->addItem(tr("None / automatic"), QString());
    f->addRow(tr("Controller"), controller_);
    Hint(f, tr("Controllers are picked up when connected. One assigned in an older version is kept here."));

    v->addWidget(Group(tr("Controller"), &f));
    invertAim_ = new QCheckBox(tr("Invert aim"));
    f->addRow(invertAim_);
    vibration_ = new QCheckBox(tr("Vibration"));
    f->addRow(vibration_);
    vibrationPercent_ = SliderRow(f, tr("Vibration strength"), 0, 100, "%");

    auto* setBox = new QGroupBox(tr("Keyboard and mouse"));
    auto* sv = new QVBoxLayout(setBox);
    {
        auto* row = new QHBoxLayout;
        singleSet_ = new QRadioButton(tr("Single player"));
        multiSet_ = new QRadioButton(tr("Multiplayer matches"));
        singleSet_->setChecked(true);
        auto* group = new QButtonGroup(setBox);
        group->addButton(singleSet_);
        group->addButton(multiSet_);
        copySingle_ = new QPushButton(tr("Copy single player's keys and mouse"));
        copySingle_->setVisible(false);
        row->addWidget(singleSet_);
        row->addWidget(multiSet_);
        row->addStretch(1);
        row->addWidget(copySingle_);
        sv->addLayout(row);
    }
    auto* mouseForm = new QFormLayout;
    mouseForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mouseLook_ = new QCheckBox(tr("Mouse look"));
    mouseForm->addRow(mouseLook_);
    mouseForm->addRow(QString(), HintLabel(tr("Move the mouse to look around. Alt+Tab frees the cursor.")));
    mouseX_ = SliderRow(mouseForm, tr("Horizontal sensitivity"), 10, 500, "%");
    mouseY_ = SliderRow(mouseForm, tr("Vertical sensitivity"), 10, 500, "%");
    invertY_ = new QCheckBox(tr("Invert Y"));
    mouseForm->addRow(invertY_);
    sv->addLayout(mouseForm);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    auto* head1 = new QLabel(tr("<b>Action</b>"));
    grid->addWidget(head1, 0, 0);
    grid->addWidget(new QLabel(tr("<b>Key</b>")), 0, 1);
    grid->addWidget(new QLabel(tr("<b>Alternative</b>")), 0, 2);
    grid->addWidget(new QLabel(tr("<b>Action</b>")), 0, 4);
    grid->addWidget(new QLabel(tr("<b>Key</b>")), 0, 5);
    grid->addWidget(new QLabel(tr("<b>Alternative</b>")), 0, 6);
    grid->setColumnMinimumWidth(3, 24);
    for (int a = 0; a < keys::kActions; a++) {
        const int half = a / (keys::kActions / 2), row = 1 + a % (keys::kActions / 2), col = half * 4;
        grid->addWidget(new QLabel(tr(keys::kActionList[a].label)), row, col);
        for (int s = 0; s < keys::kSlots; s++) {
            keyButtons_[a][s] = new KeyButton;
            grid->addWidget(keyButtons_[a][s], row, col + 1 + s);
        }
    }
    sv->addLayout(grid);
    sv->addWidget(HintLabel(tr("Click a slot, then press a key or mouse button. Esc cancels, right click clears. "
                               "Esc in the game is Start, and F1 is not used.")));
    auto* resetKeys = new QPushButton(tr("Reset these keys to defaults"));
    auto* resetRow = new QHBoxLayout;
    resetRow->addStretch(1);
    resetRow->addWidget(resetKeys);
    sv->addLayout(resetRow);
    v->addWidget(setBox);
    v->addStretch(1);

    connect(player_, &QComboBox::currentIndexChanged, this, [this] {
        StorePlayer();
        LoadPlayer();
    });
    connect(multiSet_, &QRadioButton::toggled, this, [this] {
        StorePlayer();
        LoadPlayer();
    });
    connect(resetKeys, &QPushButton::clicked, this, [this] {
        for (int a = 0; a < keys::kActions; a++) {
            const QStringList d = keys::Defaults(a);
            for (int s = 0; s < keys::kSlots; s++) keyButtons_[a][s]->SetKey(s < d.size() ? d[s] : QString());
        }
    });
    connect(copySingle_, &QPushButton::clicked, this, [this] {
        const QString single = QString("player%1").arg(shownPlayer_ + 1);
        for (int a = 0; a < keys::kActions; a++) {
            bool present = false;
            QStringList k = config_.Keys(single + ".keys." + keys::kActionList[a].key, &present);
            if (!present) k = keys::Defaults(a);
            for (int s = 0; s < keys::kSlots; s++) keyButtons_[a][s]->SetKey(s < k.size() ? k[s] : QString());
        }
        mouseLook_->setChecked(config_.Bool(single + ".mouse_look", true));
        mouseX_->setValue(config_.Int(single + ".mouse_sensitivity", 100));
        mouseY_->setValue(config_.Int(single + ".mouse_sensitivity_y", 100));
        invertY_->setChecked(config_.Bool(single + ".mouse_invert_y", false));
    });
    return page;
}

QString MainWindow::PlayerPath() const {
    const QString p = QString("player%1").arg(shownPlayer_ + 1);
    return shownMulti_ ? p + ".multiplayer" : p;
}

void MainWindow::LoadPlayer() {
    shownPlayer_ = player_->currentIndex();
    shownMulti_ = multiSet_->isChecked();
    copySingle_->setVisible(shownMulti_);
    const QString p = QString("player%1").arg(shownPlayer_ + 1);
    const QString set = PlayerPath();

    // a player's devices: player 1 has the keyboard and mouse unless the file says otherwise
    useKeyboard_->setChecked(!config_.Str(p + ".keyboard", shownPlayer_ == 0 ? "all" : "").isEmpty());
    useMouse_->setChecked(!config_.Str(p + ".mouse", shownPlayer_ == 0 ? "all" : "").isEmpty());
    SetChoice(controller_, config_.Str(p + ".controller"), "auto");
    invertAim_->setChecked(config_.Bool(p + ".invert_aim", false));
    vibration_->setChecked(config_.Bool(p + ".vibration", true));
    vibrationPercent_->setValue(config_.Int(p + ".vibration_percent", 100));

    // the multiplayer set starts as the single player one, as in the game: what it leaves out is single player's
    auto mouseValue = [&](const char* key, auto def) {
        if (shownMulti_ && !config_.Has(set + "." + key)) return p + "." + key;
        Q_UNUSED(def);
        return set + "." + key;
    };
    mouseLook_->setChecked(config_.Bool(mouseValue("mouse_look", true), true));
    mouseX_->setValue(config_.Int(mouseValue("mouse_sensitivity", 0), 100));
    mouseY_->setValue(config_.Int(mouseValue("mouse_sensitivity_y", 0), 100));
    invertY_->setChecked(config_.Bool(mouseValue("mouse_invert_y", false), false));
    for (int a = 0; a < keys::kActions; a++) {
        const QString action = keys::kActionList[a].key;
        bool present = false;
        QStringList k = config_.Keys(set + ".keys." + action, &present);
        if (!present && shownMulti_) k = config_.Keys(p + ".keys." + action, &present);
        if (!present) k = keys::Defaults(a);
        for (int s = 0; s < keys::kSlots; s++) keyButtons_[a][s]->SetKey(s < k.size() ? k[s] : QString());
    }
}

void MainWindow::StorePlayer() {
    if (loading_) return;
    const QString p = QString("player%1").arg(shownPlayer_ + 1);
    const QString set = PlayerPath();
    config_.SetStr(p + ".keyboard", useKeyboard_->isChecked() ? "all" : "");
    config_.SetStr(p + ".mouse", useMouse_->isChecked() ? "all" : "");
    config_.SetStr(p + ".controller", controller_->currentData().toString());
    config_.SetBool(p + ".invert_aim", invertAim_->isChecked());
    config_.SetBool(p + ".vibration", vibration_->isChecked());
    config_.SetInt(p + ".vibration_percent", vibrationPercent_->value());
    config_.SetBool(set + ".mouse_look", mouseLook_->isChecked());
    config_.SetInt(set + ".mouse_sensitivity", mouseX_->value());
    config_.SetInt(set + ".mouse_sensitivity_y", mouseY_->value());
    config_.SetBool(set + ".mouse_invert_y", invertY_->isChecked());
    for (int a = 0; a < keys::kActions; a++) {
        QStringList names;
        for (int s = 0; s < keys::kSlots; s++)
            if (!keyButtons_[a][s]->Key().isEmpty() && !names.contains(keyButtons_[a][s]->Key()))
                names << keyButtons_[a][s]->Key();
        config_.SetKeys(set + ".keys." + keys::kActionList[a].key, names);
    }
}

QWidget* MainWindow::BuildMultiplayer() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    router_ = new online::Router(this);

    // ── host or join ──
    auto* box = new QGroupBox(tr("Play online"));
    auto* bv = new QVBoxLayout(box);
    bv->addWidget(HintLabel(tr("One player hosts a room and shares its 6-word code; the others join with it. Then "
                               "everyone picks Multiplayer → Online Play in the game: the host chooses Host, the others "
                               "Play. Everyone needs the same version of this launcher. Players on the host's own "
                               "network don't need a code: they choose \"This network only\".")));
    auto* buttons = new QHBoxLayout;
    auto* host = new QPushButton(tr("Host a room"));
    auto* join = new QPushButton(tr("Join a room"));
    auto* local = new QPushButton(tr("This network only"));
    auto* guide = new QPushButton(tr("ℹ️ 4+ Players / LAN Guide…"));
    for (QPushButton* b : {host, join, local, guide}) b->setMinimumHeight(36);
    connect(guide, &QPushButton::clicked, this, &MainWindow::ShowMultiplayerInfoDialog);
    buttons->addWidget(host);
    buttons->addWidget(join);
    buttons->addWidget(local);
    buttons->addWidget(guide);
    buttons->addStretch(1);
    bv->addLayout(buttons);
    onlineMode_ = new QLabel;
    onlineMode_->setWordWrap(true);
    bv->addWidget(onlineMode_);

    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(mono.pointSizeF() * 1.3);
    mono.setBold(true);

    hostPanel_ = new QWidget;
    {
        auto* hv = new QVBoxLayout(hostPanel_);
        hv->setContentsMargins(0, 6, 0, 0);
        hostStatus_ = new QLabel;
        hostStatus_->setWordWrap(true);
        hostStatus_->setTextFormat(Qt::RichText);
        hv->addWidget(hostStatus_);
        auto* row = new QHBoxLayout;
        hostCode_ = new QLineEdit;
        hostCode_->setReadOnly(true);
        hostCode_->setFont(mono);
        copyCode_ = new QPushButton(tr("Copy code"));
        newCode_ = new QPushButton(tr("New code"));
        row->addWidget(hostCode_, 1);
        row->addWidget(copyCode_);
        row->addWidget(newCode_);
        hv->addLayout(row);
        protect_ = new QCheckBox(tr("Protect with a room key (recommended)"));
        protect_->setChecked(QSettings(install::IniFile(), QSettings::IniFormat).value("online/protect", true).toBool());
        hv->addWidget(protect_);
        hv->addWidget(HintLabel(tr("With the key, only players with this room's code can join from the internet, and a "
                                   "new code shuts out the old one. Without it, anyone who has your address can join "
                                   "while you host. A new code or a change here applies from the next game start.")));
        hostDetails_ = HintLabel(QString());
        hostDetails_->setVisible(false);
        hostDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        hv->addWidget(hostDetails_);
        auto* details = new QPushButton(tr("Router details…"));
        details->setFlat(true);
        connect(details, &QPushButton::clicked, this, [this] { hostDetails_->setVisible(!hostDetails_->isVisible()); });
        auto* drow = new QHBoxLayout;
        drow->addWidget(details);
        drow->addStretch(1);
        hv->addLayout(drow);
    }
    hostPanel_->setVisible(false);
    bv->addWidget(hostPanel_);

    joinPanel_ = new QWidget;
    {
        auto* jv = new QVBoxLayout(joinPanel_);
        jv->setContentsMargins(0, 6, 0, 0);
        auto* row = new QHBoxLayout;
        joinCode_ = new QLineEdit;
        joinCode_->setPlaceholderText(tr("the host's 6 words, e.g. ACORN-BLIMP-CACTUS-DOLPHIN-EASEL-FALCON"));
        joinCode_->setFont(mono);
        auto* paste = new QPushButton(tr("Paste"));
        auto* use = new QPushButton(tr("Join"));
        row->addWidget(joinCode_, 1);
        row->addWidget(paste);
        row->addWidget(use);
        jv->addLayout(row);
        joinStatus_ = new QLabel;
        joinStatus_->setWordWrap(true);
        jv->addWidget(joinStatus_);
        auto apply = [this] {
            const online::Code c = online::Decode(joinCode_->text());
            if (!c.valid) {
                joinStatus_->setText(tr("✘ That isn't a room code. Check all 6 words were copied."));
                return;
            }
            joinHost_->setText(c.publicIp.toString() + ":" + QString::number(linkPort_->value()));
            roomKey_ = c.key;
            if (SaveAll())
                joinStatus_->setText(tr("✓ Room code verified: ready to connect. Press Play, then Multiplayer → Online "
                                        "Play → Play, and pick the host's match."));
            RefreshOnlineMode();
        };
        connect(use, &QPushButton::clicked, this, apply);
        connect(joinCode_, &QLineEdit::returnPressed, this, apply);
        connect(paste, &QPushButton::clicked, this, [this, apply] {
            joinCode_->setText(QGuiApplication::clipboard()->text().trimmed());
            apply();
        });
    }
    joinPanel_->setVisible(false);
    bv->addWidget(joinPanel_);
    v->addWidget(box);

    connect(host, &QPushButton::clicked, this, &MainWindow::StartHosting);
    connect(join, &QPushButton::clicked, this, [this] {
        hostPanel_->setVisible(false);
        joinPanel_->setVisible(true);
        joinCode_->setFocus();
        RefreshOnlineMode();
    });
    connect(local, &QPushButton::clicked, this, [this] {
        router_->Close();
        joinHost_->clear();
        roomKey_ = 0;
        hostPanel_->setVisible(false);
        joinPanel_->setVisible(false);
        if (SaveAll()) RefreshOnlineMode();
    });
    connect(copyCode_, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(hostCode_->text());
        SetStatus(tr("Room code copied."));
    });
    connect(newCode_, &QPushButton::clicked, this, &MainWindow::MakeRoomCode);
    connect(protect_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings(install::IniFile(), QSettings::IniFormat).setValue("online/protect", on);
        if (!publicIp_.isNull()) MakeRoomCode();
    });
    connect(router_, &online::Router::Opened, this, [this](bool ok, const QHostAddress& ip, const QString& detail) {
        if (!ip.isNull()) {
            ShowHostCode(ip, ok, detail);
            return;
        }
        // the router did not say: ask a STUN server for the public address
        hostStatus_->setText(tr("Your router didn't open the port: %1<br>Finding your public address…").arg(detail.toHtmlEscaped()));
        router_->AskPublicAddress();
        router_->setProperty("detail", detail);
    });
    connect(router_, &online::Router::PublicAddress, this, [this](const QHostAddress& ip, const QString& error) {
        if (ip.isNull()) {
            hostStatus_->setText(tr("Couldn't find your public address: %1<br>Players on your own network can still "
                                    "join with \"This network only\".").arg(error.toHtmlEscaped()));
            hostCode_->clear();
            return;
        }
        ShowHostCode(ip, false, router_->property("detail").toString());
    });

    // ── advanced: the game's own settings ──
    QFormLayout* f;
    auto* advanced = Group(tr("Advanced"), &f);
    joinHost_ = new QLineEdit;
    joinHost_->setPlaceholderText(tr("empty to host, or to find games on this network"));
    f->addRow(tr("Host address"), joinHost_);
    Hint(f, tr("Set by a room code (then hidden). By hand: the host's address and :port, e.g. a Tailscale or ZeroTier "
               "address, or 127.0.0.1 for another copy on this PC. Typing here leaves the room."));
    binds_.push_back({[this](const Config& c) { joinHost_->setText(c.Str("join_ip")); },
                      [this](Config& c) { c.SetStr("join_ip", joinHost_->text().trimmed()); }});
    binds_.push_back({[this](const Config& c) { roomKey_ = quint32(c.Int("system_link_key", 0)) & 0xFFFFF; },
                      [this](Config& c) { c.SetInt("system_link_key", int(roomKey_)); }});
    linkPort_ = new QSpinBox;
    linkPort_->setRange(1, 65535);
    f->addRow(tr("Port (UDP)"), linkPort_);
    Hint(f, tr("3074 as on the console. Every player's game must use the same port."));
    binds_.push_back({[this](const Config& c) { linkPort_->setValue(std::clamp(c.Int("system_link_port", 3074), 1, 65535)); },
                      [this](Config& c) { c.SetInt("system_link_port", linkPort_->value()); }});
    connect(joinHost_, &QLineEdit::textEdited, this, [this] { roomKey_ = 0; RefreshOnlineMode(); });  // by hand: no room
    connect(joinHost_, &QLineEdit::textChanged, this, &MainWindow::RefreshOnlineMode);
    v->addWidget(advanced);
    v->addStretch(1);
    return page;
}

void MainWindow::StartHosting() {
    joinPanel_->setVisible(false);
    hostPanel_->setVisible(true);
    hostCode_->clear();
    hostDetails_->clear();
    joinHost_->clear();  // the host joins nobody
    publicIp_ = QHostAddress();
    RefreshOnlineMode();
    hostStatus_->setText(tr("Asking your router to open UDP port %1…").arg(linkPort_->value()));
    router_->Open(quint16(linkPort_->value()));
}

void MainWindow::MakeRoomCode() {
    if (publicIp_.isNull()) return;
    roomKey_ = protect_->isChecked() ? online::NewKey() : 0;
    online::Code c;
    c.publicIp = publicIp_;
    c.key = roomKey_;
    c.valid = true;
    hostCode_->setText(online::Encode(c));
    if (SaveAll()) RefreshOnlineMode();
}

void MainWindow::ShowHostCode(const QHostAddress& publicIp, bool portOpen, const QString& detail) {
    publicIp_ = publicIp;
    MakeRoomCode();
    const quint16 port = quint16(linkPort_->value());
    const QString lan = online::LocalAddress().toString();
    hostStatus_->setText(
        (portOpen ? tr("✔ Your router opened UDP port %1 for this PC: ready.").arg(port)
                  : tr("✘ Your router didn't open the port%1. Forward <b>UDP port %2</b> to this PC in your router's "
                       "settings (Router details below), or players from the internet can't reach you.")
                        .arg(detail.isEmpty() ? QString() : " (" + detail.toHtmlEscaped() + ")")
                        .arg(port)) +
        "<br>" + tr("Share the code, press Play, then Multiplayer → Online Play → Host. If this PC has a firewall "
                    "(ufw or firewalld on Linux, Windows Defender Firewall), allow UDP port %1. The port closes when "
                    "the game ends.").arg(port));
    // only on request: this PC's address on the home network, which the router's forwarding needs
    hostDetails_->setText(tr("Forward UDP port %1 to this PC's address on your network: %2").arg(port).arg(lan));
}

void MainWindow::RefreshOnlineMode() {
    if (!onlineMode_) return;
    const QString j = joinHost_->text().trimmed();
    // a room's host address is not shown
    joinHost_->setEchoMode(roomKey_ && !j.isEmpty() ? QLineEdit::Password : QLineEdit::Normal);
    QString text;
    if (!j.isEmpty())
        text = roomKey_ ? tr("<b>Now: joining a room</b> (from its code).")
                        : tr("<b>Now: joining %1</b> (typed by hand, no room key).").arg(j.toHtmlEscaped());
    else if (hostPanel_->isVisible() || roomKey_)
        text = roomKey_ ? tr("<b>Now: hosting a room</b> with a key: only players with its code join from the internet.")
                        : tr("<b>Now: hosting</b> without a room key: anyone with your address can join.");
    else
        text = tr("<b>Now: this network only.</b> Games are found on your local network.");
    onlineMode_->setText(text);
}

QWidget* MainWindow::BuildMods() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->addWidget(BuildDecensor());
    v->addWidget(HintLabel(
        tr("Mods replace game files in the extracted game folder (assets). The originals are set aside and put back "
           "when a mod is turned off; the disc image is never changed. Changes apply when you press Play or Apply, "
           "and the game's cache is cleared so the new files are used.")));
    modList_ = new QListWidget;
    modList_->setMinimumHeight(140);
    modList_->setWordWrap(true);
    modList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    modList_->setSpacing(4);
    v->addWidget(modList_, 1);
    modStatus_ = HintLabel(QString());
    v->addWidget(modStatus_);
    auto* row = new QHBoxLayout;
    auto* refresh = new QPushButton(tr("Refresh"));
    auto* open = new QPushButton(tr("Open mods folder"));
    auto* apply = new QPushButton(tr("Apply now"));
    row->addWidget(refresh);
    row->addWidget(open);
    row->addStretch(1);
    row->addWidget(apply);
    v->addLayout(row);
    connect(refresh, &QPushButton::clicked, this, &MainWindow::RefreshMods);
    connect(open, &QPushButton::clicked, this, [this] {
        if (!modsPath_->text().isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(modsPath_->text()));
    });
    connect(apply, &QPushButton::clicked, this, [this] { ApplyMods(false); });
    connect(modList_, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        if (loading_) return;
        QStringList on;
        for (int i = 0; i < modList_->count(); i++)
            if (modList_->item(i)->checkState() == Qt::Checked) on << modList_->item(i)->data(Qt::UserRole).toString();
        QSettings(install::IniFile(), QSettings::IniFormat).setValue("mods/enabled", on);
        RefreshMods();
    });
    return page;
}

void MainWindow::SetModEnabled(const QString& name, bool on) {
    QStringList enabled = QSettings(install::IniFile(), QSettings::IniFormat).value("mods/enabled").toStringList();
    enabled.removeAll(name);
    if (on) enabled << name;
    QSettings(install::IniFile(), QSettings::IniFormat).setValue("mods/enabled", enabled);
}

QWidget* MainWindow::BuildDecensor() {
    decensor_ = new Decensor(this);
    auto* box = new QGroupBox(tr("Decensor"));
    auto* v = new QVBoxLayout(box);
    decensorUse_ = new QCheckBox(tr("Use the decensor mod"));
    v->addWidget(decensorUse_);
    v->addWidget(HintLabel(tr("Uncensored single-player subtitles (English, French, Italian, Spanish), from "
                              "github.com/%1. Its audio is a work in progress: so far only the Great Mighty Poo song "
                              "and the Greg, Uga Buga, Spooky and Windy cutscenes are unbleeped, the rest of the "
                              "speech is still bleeped in the mod itself. The launcher downloads its latest release, "
                              "checks it against the checksum GitHub lists, and takes only the game files from it: "
                              "the release's scripts are never run.").arg(Decensor::kRepo)));
    decensorStatus_ = new QLabel;
    decensorStatus_->setWordWrap(true);
    v->addWidget(decensorStatus_);
    decensorProgress_ = new QProgressBar;
    decensorProgress_->setVisible(false);
    v->addWidget(decensorProgress_);
    auto* row = new QHBoxLayout;
    decensorAuto_ = new QCheckBox(tr("Check for updates when the launcher opens"));
    decensorAuto_->setChecked(QSettings(install::IniFile(), QSettings::IniFormat).value("decensor/autoCheck", true).toBool());
    decensorCheck_ = new QPushButton(tr("Check for updates"));
    decensorUpdate_ = new QPushButton(tr("Download"));
    decensorCancel_ = new QPushButton(tr("Cancel"));
    decensorCancel_->setVisible(false);
    row->addWidget(decensorAuto_);
    row->addStretch(1);
    row->addWidget(decensorCheck_);
    row->addWidget(decensorUpdate_);
    row->addWidget(decensorCancel_);
    v->addLayout(row);

    connect(decensorAuto_, &QCheckBox::toggled, this, [](bool on) { QSettings(install::IniFile(), QSettings::IniFormat).setValue("decensor/autoCheck", on); });
    connect(decensorCheck_, &QPushButton::clicked, this, [this] {
        decensorNote_ = tr("Checking GitHub…");
        decensor_->Check();
        RefreshDecensor();
    });
    connect(decensorUpdate_, &QPushButton::clicked, this, [this] {
        if (decensor_->Latest().valid()) decensor_->Install(modsPath_->text());
        RefreshDecensor();
    });
    connect(decensorCancel_, &QPushButton::clicked, decensor_, &Decensor::Cancel);
    connect(decensorUse_, &QCheckBox::toggled, this, [this](bool on) {
        if (loading_) return;
        SetModEnabled(Decensor::kModName, on);
        // turned on before it was ever downloaded: fetch it
        if (on && !Decensor::Installed(modsPath_->text()) && !decensor_->Busy()) {
            if (decensor_->Latest().valid()) {
                decensor_->Install(modsPath_->text());
            } else {
                decensorInstallWhenChecked_ = true;
                decensorNote_ = tr("Checking GitHub…");
                decensor_->Check();
            }
        }
        RefreshMods();
    });
    connect(decensor_, &Decensor::Checked, this, [this](bool ok, const QString& error) {
        decensorNote_ = ok ? QString() : tr("Could not check for updates: %1").arg(error);
        if (ok && decensorInstallWhenChecked_ && decensor_->UpdateAvailable(modsPath_->text()))
            decensor_->Install(modsPath_->text());
        decensorInstallWhenChecked_ = false;
        RefreshDecensor();
    });
    connect(decensor_, &Decensor::Progress, this, [this](const QString& stage, qint64 done, qint64 total) {
        decensorProgress_->setVisible(true);
        decensorProgress_->setFormat(stage + (total > 0 ? "  %p%" : QString()));
        decensorProgress_->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0) decensorProgress_->setValue(int(done * 1000 / total));
        RefreshDecensor();
    });
    connect(decensor_, &Decensor::Finished, this, [this](bool ok, const QString& message) {
        decensorProgress_->setVisible(false);
        decensorNote_ = ok ? message + " " + tr("It is put in place when you press Play or Apply now.") : message;
        RefreshMods();
        RefreshDecensor();
        if (!ok && message != tr("Download cancelled.")) QMessageBox::warning(this, tr("Decensor"), message);
    });

    return box;
}

void MainWindow::RefreshDecensor() {
    if (!decensor_) return;
    const QString dir = modsPath_->text();
    const bool busy = decensor_->Busy();
    const bool installed = Decensor::Installed(dir);
    const QString have = Decensor::InstalledVersion(dir);
    const Decensor::Release& latest = decensor_->Latest();

    QStringList lines;
    if (dir.isEmpty()) lines << tr("Set a mods folder in the Launcher tab first.");
    else if (!installed) lines << tr("Not downloaded yet.");
    else if (have.isEmpty()) lines << tr("Installed: a copy of unknown version (added by hand).");
    else lines << tr("Installed: version %1.").arg(have);
    if (latest.valid())
        lines << (decensor_->UpdateAvailable(dir)
                      ? tr("Latest release: %1 (%2, %3 MB) — %4.")
                            .arg(latest.version, latest.published)
                            .arg(latest.size / 1000000)
                            .arg(installed ? tr("update available") : tr("ready to download"))
                      : tr("Latest release: %1 — up to date.").arg(latest.version));
    if (!decensorNote_.isEmpty()) lines << decensorNote_;
    decensorStatus_->setText(lines.join("\n"));

    const bool downloading = busy && decensorProgress_->isVisible();
    decensorUse_->setEnabled(!dir.isEmpty());
    decensorCheck_->setEnabled(!busy && !dir.isEmpty());
    decensorUpdate_->setVisible(!downloading && (!latest.valid() || decensor_->UpdateAvailable(dir)));
    decensorUpdate_->setEnabled(!busy && latest.valid() && decensor_->UpdateAvailable(dir));
    decensorUpdate_->setText(latest.valid() ? (installed ? tr("Update to %1") : tr("Download %1")).arg(latest.version)
                                            : tr("Download"));
    decensorCancel_->setVisible(downloading);
    play_->setEnabled(!game_ && !decensor_->Installing() && !GameExe().isEmpty() && QFileInfo(GameExe()).isFile());
}

QWidget* MainWindow::BuildLauncher() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    QFormLayout* f;
    v->addWidget(Group(tr("Install"), &f));
    auto pathRow = [&](QLineEdit** edit, const QString& label, const QString& path) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        *edit = new QLineEdit(QDir::toNativeSeparators(path));
        (*edit)->setReadOnly(true);
        auto* open = new QPushButton(tr("Open"));
        h->addWidget(*edit, 1);
        h->addWidget(open);
        f->addRow(label, row);
        connect(open, &QPushButton::clicked, this, [path] {
            QDir().mkpath(path);
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
    };
    QLineEdit* rootEdit = nullptr;
    pathRow(&rootEdit, tr("Install folder"), install::Root());
    pathRow(&gamePath_, tr("Game"), install::GameDir());
    pathRow(&modsPath_, tr("Mods"), install::ModsDir());
    gamePath_->setText(QDir::toNativeSeparators(install::GameExe()));
    gameStatus_ = HintLabel(QString());
    gameStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    f->addRow(QString(), gameStatus_);
    {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        compilerInfo_ = new QLabel;
        compilerInfo_->setWordWrap(true);
        removeCompiler_ = new QPushButton(tr("Remove compiler"));
        h->addWidget(compilerInfo_, 1);
        h->addWidget(removeCompiler_);
        f->addRow(tr("Compiler"), row);
        connect(removeCompiler_, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, tr("Remove compiler"),
                                      tr("Remove the compiler from %1? The game keeps working; a rebuild then "
                                         "downloads it again (%2 MB).")
                                          .arg(QDir::toNativeSeparators(install::WorkDir()))
                                          .arg(install::CompilerDownloadBytes() / 1000000)) != QMessageBox::Yes)
                return;
            QDir(install::WorkDir()).removeRecursively();
            RefreshGameStatus();
        });
    }
    auto* rebuild = new QPushButton(tr("Rebuild the game…"));
    connect(rebuild, &QPushButton::clicked, this, [this] { ShowSetup(true); });
    f->addRow(rebuild);
    Hint(f, tr("Builds the game again from its extracted files (or a disc image you choose), for example after "
               "updating the launcher."));

    v->addWidget(Group(tr("Launcher"), &f));
    QSettings s(install::IniFile(), QSettings::IniFormat);
    closeOnPlay_ = new QCheckBox(tr("Close the launcher when the game starts"));
    closeOnPlay_->setChecked(s.value("launcher/closeOnPlay", false).toBool());
    connect(closeOnPlay_, &QCheckBox::toggled, this, [](bool on) { QSettings(install::IniFile(), QSettings::IniFormat).setValue("launcher/closeOnPlay", on); });
    f->addRow(closeOnPlay_);
    Hint(f, tr("Otherwise it hides while you play and comes back when the game closes."));
    {
        auto* scale = new QComboBox;
        scale->addItem(install::GamescopeSession() ? tr("Automatic (125% in Game Mode)") : tr("Automatic (100% here)"),
                       "auto");
        for (const char* p : {"100", "125", "150"}) scale->addItem(QString("%1%").arg(p), p);
        SetChoice(scale, s.value("launcher/scale", "auto").toString());
        f->addRow(tr("Launcher size"), scale);
        Hint(f, tr("Bigger text and buttons for small screens such as the Steam Deck's. Takes effect when the "
                   "launcher next opens."));
        connect(scale, &QComboBox::currentIndexChanged, this, [this, scale] {
            QSettings(install::IniFile(), QSettings::IniFormat).setValue("launcher/scale", scale->currentData());
            SetStatus(tr("The new launcher size applies the next time it opens."));
        });
    }

    // who made what this rests on
    auto* about = new QGroupBox(tr("About and credits"));
    auto* av = new QVBoxLayout(about);
    auto* credits = new QTextBrowser;
    credits->setOpenExternalLinks(true);
    QFile html(":/credits.html");
    if (html.open(QIODevice::ReadOnly)) credits->setHtml(QString::fromUtf8(html.readAll()));
    credits->setMinimumHeight(260);
    av->addWidget(credits);
    auto* licences = new QPushButton(tr("Licence texts…"));
    licences->setEnabled(!install::LicensesDir().isEmpty());
    if (!licences->isEnabled()) licences->setToolTip(tr("The licence texts ship with the AppImage."));
    auto* lrow = new QHBoxLayout;
    lrow->addStretch(1);
    lrow->addWidget(licences);
    av->addLayout(lrow);
    v->addWidget(about);
    connect(licences, &QPushButton::clicked, this, [this] {
        const QString dir = install::LicensesDir();
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Licence texts"));
        dlg.resize(900, 640);
        auto* lv = new QVBoxLayout(&dlg);
        auto* split = new QSplitter;
        auto* list = new QListWidget;
        auto* text = new QPlainTextEdit;
        text->setReadOnly(true);
        QFont mono("monospace");
        mono.setStyleHint(QFont::Monospace);
        text->setFont(mono);
        split->addWidget(list);
        split->addWidget(text);
        split->setStretchFactor(1, 3);
        lv->addWidget(split, 1);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        lv->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        QStringList files;
        while (it.hasNext()) files << QDir(dir).relativeFilePath(it.next());
        files.sort(Qt::CaseInsensitive);
        list->addItems(files);
        connect(list, &QListWidget::currentTextChanged, text, [=](const QString& rel) {
            QFile f(dir + "/" + rel);
            text->setPlainText(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
        });
        if (!files.isEmpty()) list->setCurrentRow(0);
        dlg.exec();
    });
    v->addStretch(1);
    return page;
}

void MainWindow::InstallFrom(const QString& iso) {
    ShowSetup(install::GameBuilt());
    setupPage_->StartWith(iso);
}

void MainWindow::ShowSetup(bool rebuild) {
    if (game_) return;
    setupPage_->Prepare(rebuild);
    stack_->setCurrentWidget(setupPage_);
}

// ── state ───────────────────────────────────────────────────────────────────────────────────────────────────────────

QString MainWindow::GameExe() const { return install::GameExe(); }

void MainWindow::RefreshGameStatus() {
    if (compilerInfo_ && !install::BundledCompiler().isEmpty()) {
        compilerInfo_->setText(tr("Included with the launcher: setup and rebuilds work offline."));
        removeCompiler_->setVisible(QFileInfo(install::WorkDir()).isDir());
        removeCompiler_->setText(tr("Remove build leftovers"));
    } else if (compilerInfo_) {
        const bool kept = QFileInfo(install::WorkDir()).isDir();
        compilerInfo_->setText(kept ? tr("Kept in %1 (%2 MB), so rebuilding needs no download.")
                                          .arg(QDir::toNativeSeparators(install::WorkDir()))
                                          .arg(install::FolderSize(install::WorkDir()) / 1000000)
                                    : tr("Not kept. A rebuild downloads it again (%1 MB from ziglang.org).")
                                          .arg(install::CompilerDownloadBytes() / 1000000));
        removeCompiler_->setVisible(kept);
    }
    const QString exe = GameExe();
    if (exe.isEmpty() || !QFileInfo(exe).isFile()) {
        gameStatus_->setText(tr("The game is not built yet."));
        play_->setEnabled(false);
        return;
    }
    const QString assets = paths::FindAssets(exe);
    QString text = tr("Settings: %1").arg(QDir::toNativeSeparators(paths::SettingsFile(exe)));
    if (assets.isEmpty()) text += "\n" + tr("No assets folder found beside the game or above it.");
    else
        text += "\n" + tr("Game files: %1 (%2)")
                           .arg(QDir::toNativeSeparators(assets),
                                paths::AssetsExtracted(assets) ? tr("extracted") : tr("disc image only: mods need the "
                                                                                       "extracted files"));
    gameStatus_->setText(text);
    play_->setEnabled(!game_);
}

void MainWindow::LoadAll() {
    RefreshGameStatus();
    loading_ = true;
    QString error;
    if (!GameExe().isEmpty() && !config_.Load(paths::SettingsFile(GameExe()), &error))
        QMessageBox::warning(this, tr("Settings"),
                             tr("settings.toml could not be read (%1). Defaults are shown; saving replaces it.").arg(error));
    for (const Bind& b : binds_) b.load(config_);
    LoadPlayer();
    loading_ = false;
    RefreshOnlineMode();
    RefreshMods();
}

bool MainWindow::SaveAll() {
    if (GameExe().isEmpty()) {
        SetStatus(tr("Pick the game's executable first (Launcher tab)."));
        return false;
    }
    StorePlayer();
    for (const Bind& b : binds_) b.store(config_);
    QString error;
    if (!config_.Save(&error)) {
        QMessageBox::warning(this, tr("Settings"), tr("Could not write %1: %2").arg(config_.File(), error));
        return false;
    }
    return true;
}

void MainWindow::RefreshMods() {
    loading_ = true;
    modList_->clear();
    const QStringList enabled = QSettings(install::IniFile(), QSettings::IniFormat).value("mods/enabled").toStringList();
    const QString assets = GameExe().isEmpty() ? QString() : paths::FindAssets(GameExe());
    const QStringList applied = assets.isEmpty() ? QStringList() : mods::Applied(assets);
    for (const mods::Mod& m : mods::List(modsPath_->text())) {
        QString text = m.name.left(1).toUpper() + m.name.mid(1);
        if (!m.description.isEmpty()) text += "\n" + m.description;
        text += "\n" + tr("%n file(s)", nullptr, m.files) +
                (applied.contains(m.name) ? tr(" · applied") : tr(" · not applied"));
        auto* item = new QListWidgetItem(text, modList_);
        item->setData(Qt::UserRole, m.name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(enabled.contains(m.name) ? Qt::Checked : Qt::Unchecked);
    }
    if (decensorUse_) {
        decensorUse_->setChecked(enabled.contains(Decensor::kModName));
        RefreshDecensor();
    }
    if (modList_->count() == 0)
        modStatus_->setText(tr("No mods in the mods folder. A mod is a folder laid out like the game's files "
                               "(dvddata/…), with an optional mod.txt describing it."));
    else if (!paths::AssetsExtracted(assets))
        modStatus_->setText(tr("Mods need the game's extracted files in the assets folder."));
    else
        modStatus_->setText(applied.isEmpty() ? tr("The game's files are the originals.")
                                              : tr("Applied now: %1").arg(applied.join(", ")));
    loading_ = false;
}

bool MainWindow::ApplyMods(bool quiet) {
    if (GameExe().isEmpty()) return true;
    const QString assets = paths::FindAssets(GameExe());
    const QStringList enabledNames = QSettings(install::IniFile(), QSettings::IniFormat).value("mods/enabled").toStringList();
    QList<mods::Mod> enabled;
    for (const mods::Mod& m : mods::List(modsPath_->text()))
        if (enabledNames.contains(m.name)) enabled << m;
    if (enabled.isEmpty() && (assets.isEmpty() || mods::Applied(assets).isEmpty())) return true;  // nothing to do

    QProgressDialog progress(tr("Applying mods…"), QString(), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    const mods::Result r = mods::Sync(assets, enabled, [&](int done, int total) {
        progress.setMaximum(total);
        progress.setValue(done);
        QApplication::processEvents();
    });
    progress.close();
    if (!r.ok) {
        QMessageBox::warning(this, tr("Mods"), r.error);
        RefreshMods();
        return false;
    }
    if (r.changed && !mods::ClearCache(paths::CacheDrive(GameExe())))
        QMessageBox::warning(this, tr("Mods"), tr("The game's cache (%1) could not be emptied: old sounds may play "
                                                  "until it is.").arg(paths::CacheDrive(GameExe())));
    RefreshMods();
    if (!quiet || r.changed)
        SetStatus(r.changed ? tr("Mods applied: %1 file(s) replaced, %2 restored. Game cache cleared.")
                                  .arg(r.replaced).arg(r.restored)
                            : tr("Mods are already up to date."));
    return true;
}

void MainWindow::Play() {
    if (game_) return;
    if (!install::GameBuilt()) {
        ShowSetup(false);
        return;
    }
    install::MakePortable();
    if (decensor_ && decensor_->Installing()) {
        SetStatus(tr("Wait for the decensor download to finish."));
        return;
    }
    if (!SaveAll()) return;
    if (!ApplyMods(true)) return;
    const QString exe = GameExe();
    const QString dir = QFileInfo(exe).absolutePath();
    if (closeOnPlay_->isChecked()) {
        if (!QProcess::startDetached(exe, {}, dir)) {
            QMessageBox::warning(this, tr("Play"), tr("Could not start %1").arg(exe));
            return;
        }
        close();
        return;
    }
    game_ = new QProcess(this);
    game_->setWorkingDirectory(dir);
    game_->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(game_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        game_->deleteLater();
        game_ = nullptr;
        show();
        raise();
        activateWindow();
        router_->Close();  // the hosted port goes with the game
        LoadAll();  // the game may have changed its own settings
        SetStatus(status == QProcess::CrashExit || code != 0 ? tr("The game closed unexpectedly (code %1). Its logs "
                                                                  "are in the game's log folder.").arg(code)
                                                             : tr("Welcome back."));
    });
    connect(game_, &QProcess::errorOccurred, this, [this, exe](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        QMessageBox::warning(this, tr("Play"), tr("Could not start %1").arg(exe));
        game_->deleteLater();
        game_ = nullptr;
        show();
        RefreshGameStatus();
    });
    play_->setEnabled(false);
    SetStatus(tr("Playing…"));
    game_->start(exe, {});
    hide();
}

void MainWindow::SetStatus(const QString& text) { status_->setText(text); }

void MainWindow::ShowMultiplayerInfoDialog() {
    QMessageBox box(this);
    box.setWindowTitle(tr("Split-Screen & LAN / Online Multiplayer Info"));
    box.setIcon(QMessageBox::Information);
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3 style='margin-bottom: 6px;'>Split-Screen &amp; LAN / Online Multiplayer</h3>"
        "<p><b>Important Note on Player Limits:</b><br>"
        "In <i>Conker: Live &amp; Reloaded</i>, local split-screen on one device supports a "
        "<b>maximum of 2 players</b> (in Dumbots mode).</p>"
        "<p>It is actually <b>2v2 on a LAN / Online</b> across separate PCs/instances, not 4-player split-screen on one device. "
        "The 4 controller profiles in settings exist because the original Xbox hardware had 4 controller ports, "
        "but the game engine natively caps local couch play to 2 players.</p>"
        "<hr>"
        "<h3 style='margin-bottom: 6px;'>How to Play With 4+ Players (Online Play / LAN)</h3>"
        "<p>If you have 3 or 4 players wanting to play together, you must use <b>Online Play</b> "
        "(the re-engineered System Link network play) across separate PCs/instances:</p>"
        "<ol style='margin-left: -15px; line-height: 1.5;'>"
        "<li>In the launcher, go to the <b>Multiplayer</b> tab.</li>"
        "<li>One PC clicks <b>Host a room</b> (generating a 6-word room code, or click <b>This network only</b> if everyone is on the same local network).</li>"
        "<li>Other players click <b>Join a room</b> and enter the code.</li>"
        "<li>Launch the game and go to <b>Multiplayer &rarr; Online Play</b> in-game:"
        "<ul>"
        "<li>Host chooses <b>Host</b>.</li>"
        "<li>Other players choose <b>Play</b>.</li>"
        "</ul></li>"
        "</ol>"
    ));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

// the banner gives way in a short window (a Steam Deck, a bigger launcher size): the settings and Play keep ~560 px
void MainWindow::FitBanner(int windowHeight) { banner_->setFixedHeight(std::clamp(windowHeight - 560, 56, 300)); }

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    FitBanner(height());
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (game_) {
        e->ignore();  // closing would end the game with it
        hide();
        return;
    }
    QMainWindow::closeEvent(e);
}
