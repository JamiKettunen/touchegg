/**
 * Copyright 2011 - 2020 José Expósito <jose.exposito89@gmail.com>
 *
 * This file is part of Touchégg.
 *
 * Touchégg is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License  as  published by  the  Free Software
 * Foundation,  either version 2 of the License,  or (at your option)  any later
 * version.
 *
 * Touchégg is distributed in the hope that it will be useful,  but  WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the  GNU General Public License  for more details.
 *
 * You should have received a copy of the  GNU General Public License along with
 * Touchégg. If not, see <http://www.gnu.org/licenses/>.
 */
#include "gesture-gatherer/libinput-gesture-gatherer.h"

#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>

#include "config/config.h"
#include "gesture-controller/gesture-controller-delegate.h"
#include "gesture/gesture.h"
#include "gesture/libinput-gesture.h"

LibinputGestureGatherer::LibinputGestureGatherer(
    const Config &config, GestureControllerDelegate *gestureController)
    : GestureGatherer(config, gestureController) {
  this->udevContext = udev_new();
  if (this->udevContext == nullptr) {
    throw std::runtime_error{"Error initialising Touchégg: udev"};
  }

  this->libinputContext = libinput_udev_create_context(
      &this->libinputInterface, nullptr, this->udevContext);
  if (this->libinputContext == nullptr) {
    throw std::runtime_error{"Error initialising Touchégg: libinput"};
  }

  int seat = libinput_udev_assign_seat(this->libinputContext, "seat0");
  if (seat != 0) {
    throw std::runtime_error{"Error initialising Touchégg: libinput seat"};
  }
}

LibinputGestureGatherer::~LibinputGestureGatherer() {
  if (this->libinputContext != nullptr) {
    libinput_unref(this->libinputContext);
  }

  if (this->udevContext != nullptr) {
    udev_unref(this->udevContext);
  }
}

void LibinputGestureGatherer::run() {
  int fd = libinput_get_fd(this->libinputContext);
  if (fd == -1) {
    throw std::runtime_error{"Error initialising Touchégg: libinput_get_fd"};
  }

  // Create poll to wait until libinput's file descriptor has data
  // https://man7.org/linux/man-pages/man2/poll.2.html
  int pollTimeout = -1;
  std::array<struct pollfd, 1> pollFds{{fd, POLLIN, 0}};

  while (poll(pollFds.data(), pollFds.size(), pollTimeout) >= 0) {
    // Once the data is in the file descriptor, read and process every event
    bool hasEvents = true;
    do {
      libinput_dispatch(this->libinputContext);
      struct libinput_event *event = libinput_get_event(this->libinputContext);
      if (event != nullptr) {
        this->handleEvent(event);
      } else {
        hasEvents = false;
      }
    } while (hasEvents);
  }
}

void LibinputGestureGatherer::handleEvent(struct libinput_event *event) {
  libinput_event_type eventType = libinput_event_get_type(event);
  switch (eventType) {
    case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN: {
      // TODO(jose) Use a factory to build the Gesture?
      auto gesture = std::make_unique<LibinputGesture>(event);
      this->gestureController->onGestureBegin(std::move(gesture));
      break;
    }

      // TODO(jose) Add more gesture
      // case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
      // case LIBINPUT_EVENT_GESTURE_SWIPE_END:
      // case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
      // case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
      // case LIBINPUT_EVENT_GESTURE_PINCH_END:
    default:
      break;
  }
}

int LibinputGestureGatherer::openRestricted(const char *path, int flags,
                                            void * /*userData*/) {
  int fd = open(path, flags);  // NOLINT
  if (fd < 0) {
    throw std::runtime_error{
        "Error initialising Touchégg: libinput open.\n"
        "Please execute the following command:\n"
        "$ sudo usermod -a -G input $USER\n"
        "And reboot to solve this issue"};
  }
  return fd;
}

void LibinputGestureGatherer::closeRestricted(int fd, void * /*userData*/) {
  close(fd);
}
