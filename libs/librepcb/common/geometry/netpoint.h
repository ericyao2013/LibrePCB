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

#ifndef LIBREPCB_NETPOINT_H
#define LIBREPCB_NETPOINT_H

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include "../fileio/cmd/cmdlistelementinsert.h"
#include "../fileio/cmd/cmdlistelementremove.h"
#include "../fileio/cmd/cmdlistelementsswap.h"
#include "../fileio/serializableobjectlist.h"
#include "../units/all_length_units.h"

#include <QtCore>

/*******************************************************************************
 *  Namespace / Forward Declarations
 ******************************************************************************/
namespace librepcb {

/*******************************************************************************
 *  Class NetPoint
 ******************************************************************************/

/**
 * @brief The NetPoint class
 */
class NetPoint final : public SerializableObject {
  Q_DECLARE_TR_FUNCTIONS(NetPoint)

public:
  // Signals
  enum class Event {
    UuidChanged,
    PositionChanged,
  };
  Signal<NetPoint, Event>       onEdited;
  typedef Slot<NetPoint, Event> OnEditedSlot;

  // Constructors / Destructor
  NetPoint() = delete;
  NetPoint(const NetPoint& other) noexcept;
  NetPoint(const Uuid& uuid, const NetPoint& other) noexcept;
  NetPoint(const Uuid& uuid, const Point& position) noexcept;
  explicit NetPoint(const SExpression& node);
  ~NetPoint() noexcept;

  // Getters
  const Uuid&  getUuid() const noexcept { return mUuid; }
  const Point& getPosition() const noexcept { return mPosition; }

  // Setters
  bool setPosition(const Point& position) noexcept;

  /// @copydoc librepcb::SerializableObject::serialize()
  void serialize(SExpression& root) const override;

  // Operator Overloadings
  bool operator==(const NetPoint& rhs) const noexcept;
  bool operator!=(const NetPoint& rhs) const noexcept {
    return !(*this == rhs);
  }
  NetPoint& operator=(const NetPoint& rhs) noexcept;

private:  // Data
  Uuid  mUuid;
  Point mPosition;
};

/*******************************************************************************
 *  Class NetPointList
 ******************************************************************************/

struct NetPointListNameProvider {
  static constexpr const char* tagname = "junction";
};
using NetPointList =
    SerializableObjectList<NetPoint, NetPointListNameProvider, NetPoint::Event>;
using CmdNetPointInsert =
    CmdListElementInsert<NetPoint, NetPointListNameProvider, NetPoint::Event>;
using CmdNetPointRemove =
    CmdListElementRemove<NetPoint, NetPointListNameProvider, NetPoint::Event>;
using CmdNetPointsSwap =
    CmdListElementsSwap<NetPoint, NetPointListNameProvider, NetPoint::Event>;

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace librepcb

#endif
