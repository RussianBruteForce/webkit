/*
 * Copyright (C) 2005, 2007, 2015 Apple Inc. All rights reserved.
 * Copyright (C) 2006 Jon Shier (jshier@iastate.edu)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef EventNames_h
#define EventNames_h

#include "ThreadGlobalData.h"
#include <array>
#include <functional>
#include <wtf/text/AtomicString.h>

#if defined(__has_include) && __has_include(<WebKitAdditions/EventNamesAdditions.h>)
#import <WebKitAdditions/EventNamesAdditions.h>
#endif

namespace WebCore {

#if !defined(ADDITIONAL_DOM_EVENT_NAMES_FOR_EACH)
#define ADDITIONAL_DOM_EVENT_NAMES_FOR_EACH(macro)
#endif

#define DOM_EVENT_NAMES_FOR_EACH(macro) \
    ADDITIONAL_DOM_EVENT_NAMES_FOR_EACH(macro) \
    macro(DOMActivate) \
    macro(DOMCharacterDataModified) \
    macro(DOMContentLoaded) \
    macro(DOMFocusIn) \
    macro(DOMFocusOut) \
    macro(DOMNodeInserted) \
    macro(DOMNodeInsertedIntoDocument) \
    macro(DOMNodeRemoved) \
    macro(DOMNodeRemovedFromDocument) \
    macro(DOMSubtreeModified) \
    macro(abort) \
    macro(active) \
    macro(addsourcebuffer) \
    macro(addstream) \
    macro(addtrack) \
    macro(animationend) \
    macro(animationiteration) \
    macro(animationstart) \
    macro(audioend) \
    macro(audioprocess) \
    macro(audiostart) \
    macro(autocomplete) \
    macro(autocompleteerror) \
    macro(beforecopy) \
    macro(beforecut) \
    macro(beforeload) \
    macro(beforepaste) \
    macro(beforeunload) \
    macro(beginEvent) \
    macro(blocked) \
    macro(blur) \
    macro(boundary) \
    macro(cached) \
    macro(canplay) \
    macro(canplaythrough) \
    macro(change) \
    macro(chargingchange) \
    macro(chargingtimechange) \
    macro(checking) \
    macro(click) \
    macro(close) \
    macro(complete) \
    macro(compositionend) \
    macro(compositionstart) \
    macro(compositionupdate) \
    macro(connect) \
    macro(connecting) \
    macro(contextmenu) \
    macro(copy) \
    macro(cuechange) \
    macro(cut) \
    macro(datachannel) \
    macro(dblclick) \
    macro(devicemotion) \
    macro(deviceorientation) \
    macro(dischargingtimechange) \
    macro(downloading) \
    macro(drag) \
    macro(dragend) \
    macro(dragenter) \
    macro(dragleave) \
    macro(dragover) \
    macro(dragstart) \
    macro(drop) \
    macro(durationchange) \
    macro(emptied) \
    macro(end) \
    macro(endEvent) \
    macro(ended) \
    macro(enter) \
    macro(error) \
    macro(exit) \
    macro(focus) \
    macro(focusin) \
    macro(focusout) \
    macro(gamepadconnected) \
    macro(gamepaddisconnected) \
    macro(gesturechange) \
    macro(gestureend) \
    macro(gesturescrollend) \
    macro(gesturescrollstart) \
    macro(gesturescrollupdate) \
    macro(gesturestart) \
    macro(gesturetap) \
    macro(gesturetapdown) \
    macro(hashchange) \
    macro(icecandidate) \
    macro(iceconnectionstatechange) \
    macro(icegatheringstatechange) \
    macro(inactive) \
    macro(input) \
    macro(invalid) \
    macro(keydown) \
    macro(keypress) \
    macro(keyup) \
    macro(levelchange) \
    macro(load) \
    macro(loadeddata) \
    macro(loadedmetadata) \
    macro(loadend) \
    macro(loading) \
    macro(loadingdone) \
    macro(loadstart) \
    macro(mark) \
    macro(message) \
    macro(mousedown) \
    macro(mouseenter) \
    macro(mouseleave) \
    macro(mousemove) \
    macro(mouseout) \
    macro(mouseover) \
    macro(mouseup) \
    macro(mousewheel) \
    macro(mute) \
    macro(negotiationneeded) \
    macro(nexttrack) \
    macro(nomatch) \
    macro(noupdate) \
    macro(obsolete) \
    macro(offline) \
    macro(online) \
    macro(open) \
    macro(orientationchange) \
    macro(overconstrained) \
    macro(overflowchanged) \
    macro(pagehide) \
    macro(pageshow) \
    macro(paste) \
    macro(pause) \
    macro(play) \
    macro(playing) \
    macro(pointerlockchange) \
    macro(pointerlockerror) \
    macro(popstate) \
    macro(previoustrack) \
    macro(progress) \
    macro(ratechange) \
    macro(readystatechange) \
    macro(removesourcebuffer) \
    macro(removestream) \
    macro(removetrack) \
    macro(reset) \
    macro(resize) \
    macro(result) \
    macro(resume) \
    macro(scroll) \
    macro(search) \
    macro(securitypolicyviolation) \
    macro(seeked) \
    macro(seeking) \
    macro(select) \
    macro(selectionchange) \
    macro(selectstart) \
    macro(show) \
    macro(signalingstatechange) \
    macro(soundend) \
    macro(soundstart) \
    macro(sourceclose) \
    macro(sourceended) \
    macro(sourceopen) \
    macro(speechend) \
    macro(speechstart) \
    macro(stalled) \
    macro(start) \
    macro(started) \
    macro(statechange) \
    macro(storage) \
    macro(submit) \
    macro(success) \
    macro(suspend) \
    macro(textInput) \
    macro(timeout) \
    macro(timeupdate) \
    macro(tonechange) \
    macro(touchcancel) \
    macro(touchend) \
    macro(touchmove) \
    macro(touchstart) \
    macro(track) \
    macro(transitionend) \
    macro(unload) \
    macro(unmute) \
    macro(update) \
    macro(updateend) \
    macro(updateready) \
    macro(updatestart) \
    macro(upgradeneeded) \
    macro(versionchange) \
    macro(visibilitychange) \
    macro(volumechange) \
    macro(waiting) \
    macro(webglcontextcreationerror) \
    macro(webglcontextlost) \
    macro(webglcontextrestored) \
    macro(webkitAnimationEnd) \
    macro(webkitAnimationIteration) \
    macro(webkitAnimationStart) \
    macro(webkitBeforeTextInserted) \
    macro(webkitEditableContentChanged) \
    macro(webkitTransitionEnd) \
    macro(webkitbeginfullscreen) \
    macro(webkitcurrentplaybacktargetiswirelesschanged) \
    macro(webkitdeviceproximity) \
    macro(webkitendfullscreen) \
    macro(webkitfullscreenchange) \
    macro(webkitfullscreenerror) \
    macro(webkitkeyadded) \
    macro(webkitkeyerror) \
    macro(webkitkeymessage) \
    macro(webkitmouseforcechanged) \
    macro(webkitmouseforcedown) \
    macro(webkitmouseforcewillbegin) \
    macro(webkitmouseforceup) \
    macro(webkitneedkey) \
    macro(webkitnetworkinfochange) \
    macro(webkitplaybacktargetavailabilitychanged) \
    macro(webkitpresentationmodechanged) \
    macro(webkitregionoversetchange) \
    macro(webkitremovesourcebuffer) \
    macro(webkitresourcetimingbufferfull) \
    macro(webkitsourceclose) \
    macro(webkitsourceended) \
    macro(webkitsourceopen) \
    macro(webkitspeechchange) \
    macro(webkitwillrevealbottom) \
    macro(webkitwillrevealleft) \
    macro(webkitwillrevealright) \
    macro(webkitwillrevealtop) \
    macro(wheel) \
    macro(write) \
    macro(writeend) \
    macro(writestart) \
    macro(zoom) \
// end of DOM_EVENT_NAMES_FOR_EACH

struct EventNames {
    WTF_MAKE_NONCOPYABLE(EventNames); WTF_MAKE_FAST_ALLOCATED;

public:
#define DOM_EVENT_NAMES_DECLARE(name) const AtomicString name##Event;
    DOM_EVENT_NAMES_FOR_EACH(DOM_EVENT_NAMES_DECLARE)
#undef DOM_EVENT_NAMES_DECLARE

    // FIXME: The friend declaration to std::make_unique below does not work in windows port.
    //
    // template<class T, class... Args>
    // friend typename std::_Unique_if<T>::_Single_object std::make_unique(Args&&...);
    //
    // This create function should be deleted later and is only for keeping EventNames as private.
    // std::make_unique should be used instead.
    //
    template<class... Args>
    static std::unique_ptr<EventNames> create(Args&&... args)
    {
        return std::unique_ptr<EventNames>(new EventNames(std::forward<Args>(args)...));
    }

    // FIXME: Inelegant to call these both event names and event types.
    // We should choose one term and stick to it.
    bool isWheelEventType(const AtomicString& eventType) const;
    bool isGestureEventType(const AtomicString& eventType) const;
    bool isTouchEventType(const AtomicString& eventType) const;
#if ENABLE(GAMEPAD)
    bool isGamepadEventType(const AtomicString& eventType) const;
#endif

    std::array<std::reference_wrapper<const AtomicString>, 4> touchEventNames() const;

private:
    EventNames(); // Private to prevent accidental call to EventNames() instead of eventNames().
    friend class ThreadGlobalData; // Allow ThreadGlobalData to create the per-thread EventNames object.

    int dummy; // Needed to make initialization macro work.
};

const EventNames& eventNames();

inline const EventNames& eventNames()
{
    return threadGlobalData().eventNames();
}

inline bool EventNames::isGestureEventType(const AtomicString& eventType) const
{
    return eventType == gesturestartEvent || eventType == gesturechangeEvent || eventType == gestureendEvent;
}

inline bool EventNames::isTouchEventType(const AtomicString& eventType) const
{
    return eventType == touchstartEvent
        || eventType == touchmoveEvent
        || eventType == touchendEvent
        || eventType == touchcancelEvent;
}

inline bool EventNames::isWheelEventType(const AtomicString& eventType) const
{
    return eventType == wheelEvent
        || eventType == mousewheelEvent;
}

inline std::array<std::reference_wrapper<const AtomicString>, 4> EventNames::touchEventNames() const
{
    return { { touchstartEvent, touchmoveEvent, touchendEvent, touchcancelEvent } };
}

#if ENABLE(GAMEPAD)

inline bool EventNames::isGamepadEventType(const AtomicString& eventType) const
{
    return eventType == gamepadconnectedEvent
        || eventType == gamepaddisconnectedEvent;
}

#endif

}

#endif
