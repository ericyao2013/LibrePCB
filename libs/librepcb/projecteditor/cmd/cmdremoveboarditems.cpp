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
#include "cmdremoveboarditems.h"

#include "../boardeditor/boardnetsegmentsplitter.h"
#include "cmdremoveunusedlibraryelements.h"

#include <librepcb/common/scopeguard.h>
#include <librepcb/common/toolbox.h>
#include <librepcb/project/boards/boardlayerstack.h>
#include <librepcb/project/boards/cmd/cmdboardholeremove.h>
#include <librepcb/project/boards/cmd/cmdboardnetsegmentadd.h>
#include <librepcb/project/boards/cmd/cmdboardnetsegmentaddelements.h>
#include <librepcb/project/boards/cmd/cmdboardnetsegmentremove.h>
#include <librepcb/project/boards/cmd/cmdboardplaneremove.h>
#include <librepcb/project/boards/cmd/cmdboardpolygonremove.h>
#include <librepcb/project/boards/cmd/cmdboardstroketextremove.h>
#include <librepcb/project/boards/cmd/cmddeviceinstanceremove.h>
#include <librepcb/project/boards/cmd/cmdfootprintstroketextremove.h>
#include <librepcb/project/boards/items/bi_device.h>
#include <librepcb/project/boards/items/bi_footprint.h>
#include <librepcb/project/boards/items/bi_footprintpad.h>
#include <librepcb/project/boards/items/bi_hole.h>
#include <librepcb/project/boards/items/bi_netpoint.h>
#include <librepcb/project/boards/items/bi_netsegment.h>
#include <librepcb/project/boards/items/bi_plane.h>
#include <librepcb/project/boards/items/bi_polygon.h>
#include <librepcb/project/boards/items/bi_via.h>

#include <QtCore>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {
namespace project {
namespace editor {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

CmdRemoveBoardItems::CmdRemoveBoardItems(Board& board) noexcept
  : UndoCommandGroup(tr("Remove Board Items")), mBoard(board) {
}

CmdRemoveBoardItems::~CmdRemoveBoardItems() noexcept {
}

/*******************************************************************************
 *  Inherited from UndoCommand
 ******************************************************************************/

bool CmdRemoveBoardItems::performExecute() {
  // if an error occurs, undo all already executed child commands
  auto undoScopeGuard = scopeGuard([&]() { performUndo(); });

  // also remove all netlines from devices
  foreach (BI_Device* device, mDeviceInstances) {
    Q_ASSERT(device->isAddedToBoard());
    foreach (BI_FootprintPad* pad, device->getFootprint().getPads()) {
      Q_ASSERT(pad->isAddedToBoard());
      mNetLines += pad->getNetLines();
    }
  }

  // also remove all netlines from vias
  // TODO: we shouldn't do that! but currenlty it leads to errors when not doing
  // it...
  // foreach (BI_Via* via, mVias) {
  //  Q_ASSERT(via->isAddedToBoard());
  //  mNetLines += via->getNetLines();
  //}

  // determine all affected netsegments and their items to remove
  NetSegmentItemList netSegmentItemsToRemove;
  foreach (BI_Via* via, mVias) {
    Q_ASSERT(via->isAddedToBoard());
    netSegmentItemsToRemove[&via->getNetSegment()].vias.insert(via);
  }
  foreach (BI_NetLine* netline, mNetLines) {
    Q_ASSERT(netline->isAddedToBoard());
    netSegmentItemsToRemove[&netline->getNetSegment()].netlines.insert(netline);
  }

  // remove vias/netlines/netpoints/netsegments
  for (auto it = netSegmentItemsToRemove.begin();
       it != netSegmentItemsToRemove.end(); ++it) {
    Q_ASSERT(it.key()->isAddedToBoard());
    bool removeAllVias =
        (it.value().vias == Toolbox::toSet(it.key()->getVias()));
    bool removeAllNetLines =
        (it.value().netlines == Toolbox::toSet(it.key()->getNetLines()));
    if (removeAllVias && removeAllNetLines) {
      // all items of the netsegment are selected --> remove the whole
      // netsegment
      execNewChildCmd(new CmdBoardNetSegmentRemove(*it.key()));  // can throw
    } else {
      // only some of the netsegment's lines are selected --> split up the
      // netsegment
      splitUpNetSegment(*it.key(), it.value());  // can throw
    }
  }

  // remove all device instances
  foreach (BI_Device* device, mDeviceInstances) {
    Q_ASSERT(device->isAddedToBoard());
    execNewChildCmd(new CmdDeviceInstanceRemove(*device));  // can throw
  }

  // remove planes
  foreach (BI_Plane* plane, mPlanes) {
    Q_ASSERT(plane->isAddedToBoard());
    execNewChildCmd(new CmdBoardPlaneRemove(*plane));  // can throw
  }

  // remove polygons
  foreach (BI_Polygon* polygon, mPolygons) {
    Q_ASSERT(polygon->isAddedToBoard());
    execNewChildCmd(new CmdBoardPolygonRemove(*polygon));  // can throw
  }

  // remove stroke texts
  foreach (BI_StrokeText* text, mStrokeTexts) {
    if (BI_Footprint* footprint = text->getFootprint()) {
      if (!mDeviceInstances.contains(&footprint->getDeviceInstance())) {
        Q_ASSERT(text->isAddedToBoard());
        execNewChildCmd(
            new CmdFootprintStrokeTextRemove(*footprint, *text));  // can throw
      }
    } else {
      Q_ASSERT(text->isAddedToBoard());
      execNewChildCmd(new CmdBoardStrokeTextRemove(*text));  // can throw
    }
  }

  // remove holes
  foreach (BI_Hole* hole, mHoles) {
    Q_ASSERT(hole->isAddedToBoard());
    execNewChildCmd(new CmdBoardHoleRemove(*hole));  // can throw
  }

  // remove library elements which are no longer required
  if (getChildCount() > 0) {
    execNewChildCmd(
        new CmdRemoveUnusedLibraryElements(mBoard.getProject()));  // can throw
  }

  undoScopeGuard.dismiss();  // no undo required
  return (getChildCount() > 0);
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

void CmdRemoveBoardItems::splitUpNetSegment(
    BI_NetSegment& netsegment, const NetSegmentItems& itemsToRemove) {
  // only works with segments which are added to board!!!
  Q_ASSERT(netsegment.isAddedToBoard());

  // determine all resulting sub-netsegments
  BoardNetSegmentSplitter splitter;
  foreach (BI_NetPoint* netpoint, Toolbox::toSet(netsegment.getNetPoints()) -
                                      itemsToRemove.netpoints) {
    splitter.addNetPoint(
        NetPoint(netpoint->getUuid(), netpoint->getPosition()));
  }
  foreach (BI_Via* via,
           Toolbox::toSet(netsegment.getVias()) - itemsToRemove.vias) {
    splitter.addVia(via->getVia());
  }
  foreach (BI_NetLine* netline,
           Toolbox::toSet(netsegment.getNetLines()) - itemsToRemove.netlines) {
    splitter.addTrace(netline->getTrace());
  }
  QList<BoardNetSegmentSplitter::Segment> subsegments = splitter.split();

  // remove the whole netsegment
  execNewChildCmd(new CmdBoardNetSegmentRemove(netsegment));  // can throw

  // create new sub-netsegments
  foreach (const BoardNetSegmentSplitter::Segment& subsegment, subsegments) {
    // create new netsegment
    CmdBoardNetSegmentAdd* cmdAddNetSegment = new CmdBoardNetSegmentAdd(
        netsegment.getBoard(), netsegment.getNetSignal());
    execNewChildCmd(cmdAddNetSegment);  // can throw
    BI_NetSegment* newNetSegment = cmdAddNetSegment->getNetSegment();
    Q_ASSERT(newNetSegment);
    QScopedPointer<CmdBoardNetSegmentAddElements> cmdAddElements(
        new CmdBoardNetSegmentAddElements(*newNetSegment));

    // create netpoints, assigning new UUIDs althought it wouldn't be needed
    QHash<Uuid, BI_NetPoint*> netPointMap;
    foreach (const NetPoint& netpoint, subsegment.netpoints) {
      netPointMap.insert(netpoint.getUuid(),
                         cmdAddElements->addNetPoint(netpoint.getPosition()));
    }

    // create vias, keeping original UUIDs
    QHash<Uuid, BI_Via*> viaMap;
    foreach (const Via& via, subsegment.vias) {
      viaMap.insert(via.getUuid(), cmdAddElements->addVia(via));
    }

    // create traces, keeping original UUIDs
    foreach (const Trace& trace, subsegment.traces) {
      BI_NetLineAnchor* start =
          convertAnchor(trace.getStartPoint(), netPointMap, viaMap);
      BI_NetLineAnchor* end =
          convertAnchor(trace.getEndPoint(), netPointMap, viaMap);
      GraphicsLayer* layer = mBoard.getLayerStack().getLayer(*trace.getLayer());
      if ((!start) || (!end) || (!layer)) throw LogicError(__FILE__, __LINE__);
      BI_NetLine* newNetLine =
          cmdAddElements->addNetLine(*start, *end, *layer, trace.getWidth());
      Q_ASSERT(newNetLine);
    }

    execNewChildCmd(cmdAddElements.take());  // can throw
  }
}

BI_NetLineAnchor* CmdRemoveBoardItems::convertAnchor(
    const TraceAnchor& anchor, const QHash<Uuid, BI_NetPoint*>& netPointMap,
    const QHash<Uuid, BI_Via*>& viaMap) const {
  if (const tl::optional<Uuid>& uuid = anchor.tryGetNetPoint()) {
    return netPointMap.value(*uuid);
  } else if (const tl::optional<Uuid>& uuid = anchor.tryGetVia()) {
    return viaMap.value(*uuid);
  } else if (const tl::optional<TraceAnchor::PadAnchor>& pad =
                 anchor.tryGetPad()) {
    BI_Device* device = mBoard.getDeviceInstanceByComponentUuid(pad->device);
    return device ? device->getFootprint().getPad(pad->pad) : nullptr;
  } else {
    return nullptr;
  }
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace editor
}  // namespace project
}  // namespace librepcb
