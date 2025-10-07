// Copyright (c) 2017-2025 Manuel Schneider

#pragma once
#include <pybind11/embed.h>  // Has to be imported first
#include <pybind11/stl.h> // Has to be imported first

#include <QString>
#include <QStringList>
#include <list>
namespace py = pybind11;


//  str <-> QString

namespace pybind11 {
namespace detail {

template <>
struct type_caster<QString>
{
    PYBIND11_TYPE_CASTER(QString, _("str"));
private:
    using str_caster_t = make_caster<std::u16string>;
    str_caster_t str_caster;
public:
    bool load(handle src, bool convert) {
        if (str_caster.load(src, convert)) {
            value = QString::fromStdU16String(str_caster);
            return true;
        }
        return false;
    }
    static handle cast(const QString &s, return_value_policy policy, handle parent) {
        return str_caster_t::cast(s.toStdU16String(), policy, parent);
    }
};


//  List[str] <-> QStringList

template <>
struct type_caster<QStringList> {
PYBIND11_TYPE_CASTER(QStringList, _("List[str]"));
private:
    using list_caster_t = make_caster<std::list<QString>>;
    list_caster_t list_caster;
public:
    bool load(handle src, bool convert) {
        if (list_caster.load(src, convert)) {
            auto lc = static_cast<std::list<QString>>(list_caster);
            value = QStringList(lc.cbegin(), lc.cend());
            return true;
        }
        return false;
    }
    static handle cast(const QStringList &s, return_value_policy policy, handle parent) {
        return list_caster_t::cast(std::list<QString>{s.cbegin(), s.cend()}, policy, parent);
    }
};

} // namespace detail
} // namespace pybind11
