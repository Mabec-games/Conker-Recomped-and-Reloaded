#include "config.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <sstream>

bool Config::Load(const QString& file, QString* error) {
    file_ = file;
    table_ = toml::table{};
    if (!QFileInfo::exists(file)) return true;
    try {
        table_ = toml::parse_file(file.toStdString());
    } catch (const toml::parse_error& e) {
        if (error) *error = QString::fromUtf8(e.description().data(), qsizetype(e.description().size()));
        return false;
    }
    return true;
}

bool Config::Save(QString* error) const {
    QDir().mkpath(QFileInfo(file_).absolutePath());
    std::ostringstream text;
    text << table_ << "\n";
    QSaveFile out(file_);  // written whole or not at all
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = out.errorString();
        return false;
    }
    const std::string s = text.str();
    out.write(s.data(), qint64(s.size()));
    if (!out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
}

toml::node_view<const toml::node> Config::At(const QString& path) const {
    return table_.at_path(path.toStdString());
}

bool Config::Has(const QString& path) const { return bool(At(path)); }

bool Config::Bool(const QString& path, bool def) const { return At(path).value_or(def); }

int Config::Int(const QString& path, int def) const { return int(At(path).value_or(int64_t(def))); }

QString Config::Str(const QString& path, const QString& def) const {
    if (auto v = At(path).value<std::string>()) return QString::fromStdString(*v);
    return def;
}

QStringList Config::Keys(const QString& path, bool* present) const {
    QStringList out;
    auto node = At(path);
    if (present) *present = bool(node);
    if (const toml::array* list = node.as_array()) {
        for (const toml::node& n : *list)
            if (auto s = n.value<std::string>()) out << QString::fromStdString(*s);
    } else if (auto s = node.value<std::string>()) {
        if (QString::fromStdString(*s).compare("none", Qt::CaseInsensitive) != 0) out << QString::fromStdString(*s);
    }
    return out;
}

toml::table* Config::Parent(const QString& path, std::string* leaf) {
    const QStringList parts = path.split('.');
    toml::table* t = &table_;
    for (int i = 0; i + 1 < parts.size(); i++) {
        const std::string key = parts[i].toStdString();
        toml::table* next = (*t)[key].as_table();
        if (!next) next = t->insert_or_assign(key, toml::table{}).first->second.as_table();
        t = next;
    }
    *leaf = parts.last().toStdString();
    return t;
}

void Config::SetBool(const QString& path, bool v) {
    std::string leaf;
    Parent(path, &leaf)->insert_or_assign(leaf, v);
}

void Config::SetInt(const QString& path, int v) {
    std::string leaf;
    Parent(path, &leaf)->insert_or_assign(leaf, int64_t(v));
}

void Config::SetStr(const QString& path, const QString& v) {
    std::string leaf;
    Parent(path, &leaf)->insert_or_assign(leaf, v.toStdString());
}

void Config::SetKeys(const QString& path, const QStringList& names) {
    std::string leaf;
    toml::table* t = Parent(path, &leaf);
    if (names.isEmpty()) {
        t->insert_or_assign(leaf, "none");
    } else if (names.size() == 1) {
        t->insert_or_assign(leaf, names.first().toStdString());
    } else {
        toml::array list;
        for (const QString& n : names) list.push_back(n.toStdString());
        t->insert_or_assign(leaf, std::move(list));
    }
}
