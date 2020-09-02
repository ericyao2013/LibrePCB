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
#include "cmdpasteboarditems.h"

#include "../boardeditor/boardclipboarddata.h"
#include "cmdremoveboarditems.h"

#include <librepcb/common/scopeguard.h>
#include <librepcb/common/toolbox.h>
#include <librepcb/library/dev/device.h>
#include <librepcb/library/pkg/package.h>
#include <librepcb/project/boards/boardlayerstack.h>
#include <librepcb/project/boards/cmd/cmdboardholeadd.h>
#include <librepcb/project/boards/cmd/cmdboardnetsegmentadd.h>
#include <librepcb/project/boards/cmd/cmdboardnetsegmentaddelements.h>
#include <librepcb/project/boards/cmd/cmdboardplaneadd.h>
#include <librepcb/project/boards/cmd/cmdboardpolygonadd.h>
#include <librepcb/project/boards/cmd/cmdboardstroketextadd.h>
#include <librepcb/project/boards/cmd/cmddeviceinstanceadd.h>
#include <librepcb/project/boards/items/bi_device.h>
#include <librepcb/project/boards/items/bi_footprint.h>
#include <librepcb/project/boards/items/bi_footprintpad.h>
#include <librepcb/project/boards/items/bi_hole.h>
#include <librepcb/project/boards/items/bi_netline.h>
#include <librepcb/project/boards/items/bi_netpoint.h>
#include <librepcb/project/boards/items/bi_netsegment.h>
#include <librepcb/project/boards/items/bi_plane.h>
#include <librepcb/project/boards/items/bi_polygon.h>
#include <librepcb/project/boards/items/bi_stroketext.h>
#include <librepcb/project/boards/items/bi_via.h>
#include <librepcb/project/circuit/circuit.h>
#include <librepcb/project/circuit/cmd/cmdnetclassadd.h>
#include <librepcb/project/circuit/cmd/cmdnetsignaladd.h>
#include <librepcb/project/circuit/netsignal.h>
#include <librepcb/project/library/cmd/cmdprojectlibraryaddelement.h>
#include <librepcb/project/library/projectlibrary.h>
#include <librepcb/project/project.h>

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

CmdPasteBoardItems::CmdPasteBoardItems(Board& board,
                                       std::unique_ptr<BoardClipboardData> data,
                                       const Point& posOffset) noexcept
  : UndoCommandGroup(tr("Paste Board Elements")),
    mProject(board.getProject()),
    mBoard(board),
    mData(std::move(data)),
    mPosOffset(posOffset) {
  Q_ASSERT(mData);
}

CmdPasteBoardItems::~CmdPasteBoardItems() noexcept {
}

/*******************************************************************************
 *  Inherited from UndoCommand
 ******************************************************************************/

bool CmdPasteBoardItems::performExecute() {
  // if an error occurs, undo all already executed child commands
  auto undoScopeGuard = scopeGuard([&]() { performUndo(); });

  // Notes:
  //
  //  - Devices are only pasted if the corresponding component exists in the
  //    circuit, and the device does not yet exist on the board (one cannot
  //    paste a device if it is already added to the board).
  //  - Netlines which were attached to a pad or via which was not copy/pasted
  //    will be attached to newly created freestanding netpoints.
  //  - The graphics items of the added elements are selected immediately to
  //    allow dragging them afterwards.

  // Paste devices which do not yet exist on the board
  QSet<Uuid> pastedDevices;
  for (const BoardClipboardData::Device& dev : mData->getDevices()) {
    ComponentInstance* cmpInst =
        mProject.getCircuit().getComponentInstanceByUuid(dev.componentUuid);
    if (!cmpInst) {
      continue;  // Corresponding component does not exist (anymore) in circuit.
    }
    BI_Device* devInst =
        mBoard.getDeviceInstanceByComponentUuid(dev.componentUuid);
    if (devInst) {
      continue;  // Device already exist on the board.
    }

    // Copy new device to project library, if not existing already
    tl::optional<Uuid> pgkUuid;
    if (const library::Device* libDev =
            mProject.getLibrary().getDevice(dev.libDeviceUuid)) {
      pgkUuid = libDev->getPackageUuid();
    } else {
      QScopedPointer<library::Device> newLibDev(new library::Device(
          mData->getDirectory("dev/" % dev.libDeviceUuid.toStr())));
      pgkUuid = newLibDev->getPackageUuid();
      execNewChildCmd(new CmdProjectLibraryAddElement<library::Device>(
          mProject.getLibrary(), *newLibDev.take()));
    }
    Q_ASSERT(pgkUuid);

    // Copy new package to project library, if not existing already
    if (!mProject.getLibrary().getPackage(*pgkUuid)) {
      QScopedPointer<library::Package> newLibPgk(
          new library::Package(mData->getDirectory("pkg/" % pgkUuid->toStr())));
      execNewChildCmd(new CmdProjectLibraryAddElement<library::Package>(
          mProject.getLibrary(), *newLibPgk.take()));
    }

    // Add device instance to board
    QScopedPointer<BI_Device> device(
        new BI_Device(mBoard, *cmpInst, dev.libDeviceUuid, dev.libFootprintUuid,
                      dev.position + mPosOffset, dev.rotation, dev.mirrored));
    foreach (BI_StrokeText* text, device->getFootprint().getStrokeTexts()) {
      device->getFootprint().removeStrokeText(*text);
    }
    for (const StrokeText& text : dev.strokeTexts) {
      StrokeText copy(Uuid::createRandom(), text);        // assign new UUID
      copy.setPosition(copy.getPosition() + mPosOffset);  // move
      BI_StrokeText* item = new BI_StrokeText(mBoard, copy);
      item->setSelected(true);
      device->getFootprint().addStrokeText(*item);
    }
    device->setSelected(true);
    execNewChildCmd(new CmdDeviceInstanceAdd(*device.take()));
    pastedDevices.insert(dev.componentUuid);
  }

  // Paste net segments
  for (const BoardClipboardData::NetSegment& seg : mData->getNetSegments()) {
    // Add new segment
    BI_NetSegment* copy =
        new BI_NetSegment(mBoard, *getOrCreateNetSignal(*seg.netName));
    copy->setSelected(true);
    execNewChildCmd(new CmdBoardNetSegmentAdd(*copy));

    // Add vias, netpoints and netlines
    QScopedPointer<CmdBoardNetSegmentAddElements> cmdAddElements(
        new CmdBoardNetSegmentAddElements(*copy));
    QHash<Uuid, BI_Via*> viaMap;
    for (const Via& v : seg.vias) {
      BI_Via* via = cmdAddElements->addVia(
          Via(Uuid::createRandom(), v.getPosition() + mPosOffset, v.getShape(),
              v.getSize(), v.getDrillDiameter()));
      via->setSelected(true);
      viaMap.insert(v.getUuid(), via);
    }
    QHash<Uuid, BI_NetPoint*> netPointMap;
    for (const NetPoint& np : seg.points) {
      BI_NetPoint* netpoint =
          cmdAddElements->addNetPoint(np.getPosition() + mPosOffset);
      netpoint->setSelected(true);
      netPointMap.insert(np.getUuid(), netpoint);
    }
    QMap<std::pair<Uuid, Uuid>, BI_NetLineAnchor*>
        replacedPads;  // Key: component, pad
    for (const Trace& trace : seg.traces) {
      BI_NetLineAnchor* start = nullptr;
      // switch (trace.startType) {
      //  case BoardClipboardData::NetLine::JunctionType::NetPoint: {
      //    start = netPointMap[*trace.startUuid1];
      //    break;
      //  }
      //  case BoardClipboardData::NetLine::JunctionType::Via: {
      //    start = viaMap[*trace.startUuid1];
      //    break;
      //  }
      //  case BoardClipboardData::NetLine::JunctionType::FootprintPadSmt:
      //  case BoardClipboardData::NetLine::JunctionType::FootprintPadTht: {
      //    BI_Device* device =
      //        mBoard.getDeviceInstanceByComponentUuid(*trace.startUuid1);
      //    if ((!device) || (!pastedDevices.contains(*trace.startUuid1))) {
      //      // Connected footprint was not pasted or does not even exist, so
      //      // we have to replace the pad with a new via, which will be
      //      removed
      //      // later (a bit ugly, but way simpler to implement).
      //      auto key = std::make_pair(*trace.startUuid1, *trace.startUuid2);
      //      start    = replacedPads.value(key);
      //      if (!start) {
      //        if (trace.startType ==
      //            BoardClipboardData::NetLine::JunctionType::FootprintPadTht)
      //            {
      //          // Add via
      //          BI_Via* via = cmdAddElements->addVia(
      //              Via(Uuid::createRandom(), trace.startPos + mPosOffset,
      //                  Via::Shape::Round, PositiveLength(800000),
      //                  PositiveLength(300000)));
      //          via->setSelected(true);
      //          start = via;
      //        } else {
      //          // Add netpoint
      //          BI_NetPoint* np =
      //              cmdAddElements->addNetPoint(trace.startPos + mPosOffset);
      //          np->setSelected(true);
      //          start = np;
      //        }
      //        replacedPads[key] = start;
      //      }
      //    } else {
      //      start = device->getFootprint().getPad(*trace.startUuid2);
      //    }
      //    break;
      //  }
      //  default:
      //    break;
      //}
      BI_NetLineAnchor* end = nullptr;
      // switch (trace.endType) {
      //  case BoardClipboardData::NetLine::JunctionType::NetPoint: {
      //    end = netPointMap[*trace.endUuid1];
      //    break;
      //  }
      //  case BoardClipboardData::NetLine::JunctionType::Via: {
      //    end = viaMap[*trace.endUuid1];
      //    break;
      //  }
      //  case BoardClipboardData::NetLine::JunctionType::FootprintPadSmt:
      //  case BoardClipboardData::NetLine::JunctionType::FootprintPadTht: {
      //    BI_Device* device =
      //        mBoard.getDeviceInstanceByComponentUuid(*trace.endUuid1);
      //    if ((!device) || (!pastedDevices.contains(*trace.endUuid1))) {
      //      // Connected footprint was not pasted or does not even exist, so
      //      // we have to replace the pad with a new via, which will be
      //      removed
      //      // later (a bit ugly, but way simpler to implement).
      //      auto key = std::make_pair(*trace.endUuid1, *trace.endUuid2);
      //      end      = replacedPads.value(key);
      //      if (!end) {
      //        if (trace.endType ==
      //            BoardClipboardData::NetLine::JunctionType::FootprintPadTht)
      //            {
      //          // Add via
      //          BI_Via* via = cmdAddElements->addVia(
      //              Via(Uuid::createRandom(), trace.endPos + mPosOffset,
      //                  Via::Shape::Round, PositiveLength(800000),
      //                  PositiveLength(300000)));
      //          via->setSelected(true);
      //          end = via;
      //        } else {
      //          // Add netpoint
      //          BI_NetPoint* np =
      //              cmdAddElements->addNetPoint(trace.endPos + mPosOffset);
      //          np->setSelected(true);
      //          end = np;
      //        }
      //        replacedPads[key] = end;
      //      }
      //    } else {
      //      end = device->getFootprint().getPad(*trace.endUuid2);
      //    }
      //    break;
      //  }
      //  default:
      //    break;
      //}
      GraphicsLayer* layer = mBoard.getLayerStack().getLayer(*trace.getLayer());
      if ((!start) || (!end) || (!layer)) throw LogicError(__FILE__, __LINE__);
      BI_NetLine* netline =
          cmdAddElements->addNetLine(*start, *end, *layer, trace.getWidth());
      netline->setSelected(true);
    }
    execNewChildCmd(cmdAddElements.take());
    // Remove the vias which were added as temporary footprint pad replacements.
    // QScopedPointer<CmdRemoveBoardItems> cmdRemoveTemporaryVias(
    //    new CmdRemoveBoardItems(mBoard));
    // cmdRemoveTemporaryVias->removeVias(Toolbox::toSet(replacedPads.values()));
    // execNewChildCmd(cmdRemoveTemporaryVias.take());
  }

  // Paste planes
  for (const BoardClipboardData::Plane& plane : mData->getPlanes()) {
    BI_Plane* copy = new BI_Plane(mBoard,
                                  Uuid::createRandom(),  // assign new UUID
                                  GraphicsLayerName(plane.layer),
                                  *getOrCreateNetSignal(plane.netSignalName),
                                  plane.outline.translated(mPosOffset)  // move
    );
    copy->setMinWidth(plane.minWidth);
    copy->setMinClearance(plane.minClearance);
    copy->setKeepOrphans(plane.keepOrphans);
    copy->setPriority(plane.priority);
    copy->setConnectStyle(plane.connectStyle);
    copy->setSelected(true);
    execNewChildCmd(new CmdBoardPlaneAdd(*copy));
  }

  // Paste polygons
  for (const Polygon& polygon : mData->getPolygons()) {
    Polygon copy(Uuid::createRandom(), polygon);          // assign new UUID
    copy.setPath(copy.getPath().translated(mPosOffset));  // move
    BI_Polygon* item = new BI_Polygon(mBoard, copy);
    item->setSelected(true);
    execNewChildCmd(new CmdBoardPolygonAdd(*item));
  }

  // Paste stroke texts
  for (const StrokeText& text : mData->getStrokeTexts()) {
    StrokeText copy(Uuid::createRandom(), text);        // assign new UUID
    copy.setPosition(copy.getPosition() + mPosOffset);  // move
    BI_StrokeText* item = new BI_StrokeText(mBoard, copy);
    item->setSelected(true);
    execNewChildCmd(new CmdBoardStrokeTextAdd(*item));
  }

  // Paste holes
  for (const Hole& hole : mData->getHoles()) {
    Hole copy(Uuid::createRandom(), hole);              // assign new UUID
    copy.setPosition(copy.getPosition() + mPosOffset);  // move
    BI_Hole* item = new BI_Hole(mBoard, copy);
    item->setSelected(true);
    execNewChildCmd(new CmdBoardHoleAdd(*item));
  }

  undoScopeGuard.dismiss();  // no undo required
  return getChildCount() > 0;
}

NetSignal* CmdPasteBoardItems::getOrCreateNetSignal(const QString& name) {
  NetSignal* netSignal = mProject.getCircuit().getNetSignalByName(name);
  if (netSignal) {
    return netSignal;
  }

  // Get or create netclass with the name "default"
  NetClass* netclass =
      mProject.getCircuit().getNetClassByName(ElementName("default"));
  if (!netclass) {
    CmdNetClassAdd* cmd =
        new CmdNetClassAdd(mProject.getCircuit(), ElementName("default"));
    execNewChildCmd(cmd);
    netclass = cmd->getNetClass();
    Q_ASSERT(netclass);
  }

  // Create new net signal
  CmdNetSignalAdd* cmdAddNetSignal =
      new CmdNetSignalAdd(mProject.getCircuit(), *netclass);
  execNewChildCmd(cmdAddNetSignal);
  return cmdAddNetSignal->getNetSignal();
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace editor
}  // namespace project
}  // namespace librepcb
