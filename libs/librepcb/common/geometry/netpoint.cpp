/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 LibrePCB Developers, see AUTHORS.md for contributors.
 * https://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include "netpoint.h"

#include <QtCore>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

NetPoint::NetPoint(const NetPoint& other) noexcept
  : onEdited(*this), mUuid(other.mUuid), mPosition(other.mPosition) {
}

NetPoint::NetPoint(const Uuid& uuid, const NetPoint& other) noexcept
  : NetPoint(other) {
  mUuid = uuid;
}

NetPoint::NetPoint(const Uuid& uuid, const Point& position) noexcept
  : onEdited(*this), mUuid(uuid), mPosition(position) {
}

NetPoint::NetPoint(const SExpression& node)
  : onEdited(*this),
    mUuid(node.getChildByIndex(0).getValue<Uuid>()),
    mPosition(node.getChildByPath("position")) {
}

NetPoint::~NetPoint() noexcept {
}

/*******************************************************************************
 *  Setters
 ******************************************************************************/

bool NetPoint::setPosition(const Point& position) noexcept {
  if (position == mPosition) {
    return false;
  }

  mPosition = position;
  onEdited.notify(Event::PositionChanged);
  return true;
}

/*******************************************************************************
 *  General Methods
 ******************************************************************************/

void NetPoint::serialize(SExpression& root) const {
  root.appendChild(mUuid);
  root.appendChild(mPosition.serializeToDomElement("position"), false);
}

/*******************************************************************************
 *  Operator Overloadings
 ******************************************************************************/

bool NetPoint::operator==(const NetPoint& rhs) const noexcept {
  if (mUuid != rhs.mUuid) return false;
  if (mPosition != rhs.mPosition) return false;
  return true;
}

NetPoint& NetPoint::operator=(const NetPoint& rhs) noexcept {
  if (mUuid != rhs.mUuid) {
    mUuid = rhs.mUuid;
    onEdited.notify(Event::UuidChanged);
  }
  setPosition(rhs.mPosition);
  return *this;
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace librepcb
