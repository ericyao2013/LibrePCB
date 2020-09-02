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
#include "boardclipboarddatabuilder.h"

#include "boardnetsegmentsplitter.h"

#include <librepcb/common/geometry/polygon.h>
#include <librepcb/common/graphics/graphicslayer.h>
#include <librepcb/library/dev/device.h>
#include <librepcb/library/pkg/package.h>
#include <librepcb/project/boards/board.h>
#include <librepcb/project/boards/boardselectionquery.h>
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
#include <librepcb/project/circuit/netsignal.h>

#include <QtCore>
#include <QtWidgets>

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {
namespace project {
namespace editor {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

BoardClipboardDataBuilder::BoardClipboardDataBuilder(Board& board) noexcept
  : mBoard(board) {
}

BoardClipboardDataBuilder::~BoardClipboardDataBuilder() noexcept {
}

/*******************************************************************************
 *  General Methods
 ******************************************************************************/

std::unique_ptr<BoardClipboardData> BoardClipboardDataBuilder::generate(
    const Point& cursorPos) const noexcept {
  std::unique_ptr<BoardClipboardData> data(
      new BoardClipboardData(mBoard.getUuid(), cursorPos));

  // get all selected items
  std::unique_ptr<BoardSelectionQuery> query(mBoard.createSelectionQuery());
  query->addDeviceInstancesOfSelectedFootprints();
  query->addSelectedVias();
  query->addSelectedNetLines();
  query->addSelectedPlanes();
  query->addSelectedPolygons();
  query->addSelectedBoardStrokeTexts();
  query->addSelectedHoles();

  // add devices
  foreach (BI_Device* device, query->getDeviceInstances()) {
    // copy library device
    std::unique_ptr<TransactionalDirectory> devDir =
        data->getDirectory("dev/" % device->getLibDevice().getUuid().toStr());
    if (devDir->getFiles().isEmpty()) {
      device->getLibDevice().getDirectory().copyTo(*devDir);
    }
    // copy library package
    std::unique_ptr<TransactionalDirectory> pkgDir =
        data->getDirectory("pkg/" % device->getLibPackage().getUuid().toStr());
    if (pkgDir->getFiles().isEmpty()) {
      device->getLibPackage().getDirectory().copyTo(*pkgDir);
    }
    // create list of stroke texts
    StrokeTextList strokeTexts;
    foreach (const BI_StrokeText* t, device->getFootprint().getStrokeTexts()) {
      strokeTexts.append(std::make_shared<StrokeText>(t->getText()));
    }
    // add device
    data->getDevices().append(std::make_shared<BoardClipboardData::Device>(
        device->getComponentInstanceUuid(), device->getLibDevice().getUuid(),
        device->getLibFootprint().getUuid(), device->getPosition(),
        device->getRotation(), device->getIsMirrored(), strokeTexts));
  }

  // add (splitted) net segments including netpoints, netlines and netlabels
  /*foreach (BI_NetSegment* netsegment, mBoard.getNetSegments()) {
    BoardNetSegmentSplitter splitter;
    foreach (BI_Via* via, query->getVias()) {
      if (&via->getNetSegment() == netsegment) {
        splitter.addVia(via);
      }
    }
    foreach (BI_NetLine* netline, query->getNetLines()) {
      if (&netline->getNetSegment() == netsegment) {
        splitter.addNetLine(netline);
      }
    }

    foreach (const BoardNetSegmentSplitter::Segment& seg, splitter.split()) {
      std::shared_ptr<BoardClipboardData::NetSegment> newSegment =
          std::make_shared<BoardClipboardData::NetSegment>(
              netsegment->getNetSignal().getName());
      data->getNetSegments().append(newSegment);

      QHash<BI_NetLineAnchor*, std::shared_ptr<NetPoint>> replacedNetPoints;
      foreach (BI_NetLineAnchor* anchor, seg.anchors) {
        if (BI_NetPoint* np = dynamic_cast<BI_NetPoint*>(anchor)) {
          newSegment->points.append(
              std::make_shared<NetPoint>(np->getUuid(), np->getPosition()));
        } else if (BI_Via* via = dynamic_cast<BI_Via*>(anchor)) {
          if (query->getVias().contains(via)) {
            newSegment->vias.append(std::make_shared<Via>(via->getVia()));
          } else {
            // Via will not be copied, thus replacing it by a netpoint
            std::shared_ptr<NetPoint> np = std::make_shared<NetPoint>(
                Uuid::createRandom(), via->getPosition());
            replacedNetPoints.insert(via, np);
            newSegment->points.append(np);
          }
        } else if (BI_FootprintPad* pad =
                       dynamic_cast<BI_FootprintPad*>(anchor)) {
          if (!query->getDeviceInstances().contains(
                  &pad->getFootprint().getDeviceInstance())) {
            // Pad will not be copied, thus replacing it by a netpoint
            std::shared_ptr<NetPoint> np = std::make_shared<NetPoint>(
                Uuid::createRandom(), pad->getPosition());
            replacedNetPoints.insert(pad, np);
            newSegment->points.append(np);
          }
        }
      }
      foreach (BI_NetLine* netline, seg.netlines) {
        tl::optional<TraceAnchor> startAnchor;
        if (BI_NetPoint* netpoint =
                dynamic_cast<BI_NetPoint*>(&netline->getStartPoint())) {
          startAnchor = TraceAnchor::netPoint(netpoint->getUuid());
        } else if (BI_Via* via =
                       dynamic_cast<BI_Via*>(&netline->getStartPoint())) {
          if (auto np = replacedNetPoints.value(via)) {
            startAnchor = TraceAnchor::netPoint(np->getUuid());
          } else {
            startAnchor = TraceAnchor::via(via->getUuid());
          }
        } else if (BI_FootprintPad* pad = dynamic_cast<BI_FootprintPad*>(
                       &netline->getStartPoint())) {
          if (auto np = replacedNetPoints.value(pad)) {
            startAnchor = TraceAnchor::netPoint(np->getUuid());
          } else {
            startAnchor = TraceAnchor::pad(pad->getFootprint()
                                               .getDeviceInstance()
                                               .getComponentInstanceUuid(),
                                           pad->getLibPadUuid());
          }
        } else {
          Q_ASSERT(false);
        }
        Q_ASSERT(startAnchor);

        tl::optional<TraceAnchor> endAnchor;
        if (BI_NetPoint* netpoint =
                dynamic_cast<BI_NetPoint*>(&netline->getEndPoint())) {
          endAnchor = TraceAnchor::netPoint(netpoint->getUuid());
        } else if (BI_Via* via =
                       dynamic_cast<BI_Via*>(&netline->getEndPoint())) {
          if (auto np = replacedNetPoints.value(via)) {
            endAnchor = TraceAnchor::netPoint(np->getUuid());
          } else {
            endAnchor = TraceAnchor::via(via->getUuid());
          }
        } else if (BI_FootprintPad* pad = dynamic_cast<BI_FootprintPad*>(
                       &netline->getEndPoint())) {
          if (auto np = replacedNetPoints.value(pad)) {
            endAnchor = TraceAnchor::netPoint(np->getUuid());
          } else {
            endAnchor = TraceAnchor::pad(pad->getFootprint()
                                             .getDeviceInstance()
                                             .getComponentInstanceUuid(),
                                         pad->getLibPadUuid());
          }
        } else {
          Q_ASSERT(false);
        }
        Q_ASSERT(endAnchor);

        std::shared_ptr<Trace> copy = std::make_shared<Trace>(
            netline->getUuid(),
            GraphicsLayerName(netline->getLayer().getName()),
            netline->getWidth(), *startAnchor, *endAnchor);
        newSegment->traces.append(copy);
      }
    }
  }*/

  // add planes
  foreach (BI_Plane* plane, query->getPlanes()) {
    std::shared_ptr<BoardClipboardData::Plane> newPlane =
        std::make_shared<BoardClipboardData::Plane>(
            plane->getUuid(), *plane->getLayerName(),
            *plane->getNetSignal().getName(), plane->getOutline(),
            plane->getMinWidth(), plane->getMinClearance(),
            plane->getKeepOrphans(), plane->getPriority(),
            plane->getConnectStyle());
    data->getPlanes().append(newPlane);
  }

  // add polygons
  foreach (BI_Polygon* polygon, query->getPolygons()) {
    data->getPolygons().append(
        std::make_shared<Polygon>(polygon->getPolygon()));
  }

  // add stroke texts
  foreach (BI_StrokeText* text, query->getStrokeTexts()) {
    data->getStrokeTexts().append(
        std::make_shared<StrokeText>(text->getText()));
  }

  // add holes
  foreach (BI_Hole* hole, query->getHoles()) {
    data->getHoles().append(std::make_shared<Hole>(hole->getHole()));
  }

  return data;
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace editor
}  // namespace project
}  // namespace librepcb
