/*
 * QEstEidCommon
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#pragma once

#include <QtCore/QStringList>

class Common
{
public:
	static QString applicationOs();
	static QStringList drivers();
	/**
	 * @brief Sets the application language reported by userAgent()
	 *
	 * When unset the locale is reported instead.
	 */
	static void setLanguage(const QString &lang);
	/**
	 * @brief User-Agent header value following the RIA User-Agent Header Specification (schema 1)
	 * @param devices include the connected card readers in the metadata block
	 */
	static QByteArray userAgent(bool devices = false);

};
