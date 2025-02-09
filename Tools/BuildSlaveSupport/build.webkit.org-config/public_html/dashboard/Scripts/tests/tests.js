/*
 * Copyright (C) 2016 Apple, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

module("Trac", {
    setup: function() {
        this.trac = new MockTrac();
    }
});

test("_loaded", function()
{
    var client = new XMLHttpRequest();
    client.open('GET', 'test-fixture-trac-rss.xml', false);
    client.onreadystatechange = function () {
        if (client.readyState === client.DONE)
            this.trac._loaded(client.responseXML);
    }.bind(this);
    client.send();
    var commits = this.trac.recordedCommits;
    strictEqual(commits.length, 8, "should have 8 commits");
    for (var i = 1; i < commits.length; i++) {
        var firstRevision = commits[i - 1].revisionNumber;
        var secondRevision = commits[i].revisionNumber;
        strictEqual(secondRevision - firstRevision, 1, "commits should be in order " + firstRevision + ", " + secondRevision);
    }
});

test("_parseRevisionFromURL", function()
{
    strictEqual(this.trac._parseRevisionFromURL("https://trac.webkit.org/changeset/190497"), "190497", "Subversion");
    strictEqual(this.trac._parseRevisionFromURL("http://trac.foobar.com/repository/changeset/75388/project"), "75388", "Subversion with suffix");
    strictEqual(this.trac._parseRevisionFromURL("https://git.foobar.com/trac/Whatever.git/changeset/0e498db5d8e5b5a342631"), "0e498db5d8e5b5a342631", "Git");
});

module("BuildBotQueueView", {
    setup: function() {
        this.trac = new MockTrac();
        this.queue = new MockBuildbotQueue();
        this.trunkBranch = {
            name: "trunk",
            repository: {
                name: "openSource",
                trac: this.trac,
                isSVN: true,
            }
        };
        this.queue.branches = [this.trunkBranch];
        this.view = new MockBuildbotQueueView([this.queue]);
    }
});

var settings = new Settings;
test("_appendPendingRevisionCount", function()
{
    this.view._appendPendingRevisionCount(this.queue);
    var revisionsBehind = this.view.element.getElementsByClassName("message")[0].innerHTML.match(/.*(\d+) revision(|s) behind/)[1];
    equal(revisionsBehind, "1", "assert revisions behind");
});

test("_popoverLinesForCommitRange", function()
{
    var lines = this.view._popoverLinesForCommitRange(this.trac, this.trunkBranch, 33018, 33020);
    strictEqual(lines.length, 3, "has 3 lines");
});

test("_presentPopoverForPendingCommits", function()
{
    var element = document.createElement("div");
    var popover = new Dashboard.Popover();
    this.view._presentPopoverForPendingCommits(element, popover, this.queue);
    var nodeList = popover._element.getElementsByClassName("pending-commit");
    strictEqual(nodeList.length, 1, "has 1 pending commit");
});

test("_presentPopoverForRevisionRange", function()
{
    var element = document.createElement("div");
    var popover = new Dashboard.Popover();
    var context = {
        trac: this.trac,
        branch: this.trunkBranch,
        firstRevision: 33018,
        lastRevision: 33020
    };
    this.view._presentPopoverForRevisionRange(element, popover, context);
    var nodeList = popover._element.getElementsByClassName("pending-commit");
    strictEqual(nodeList.length, 3, "has 3 commits");
});

test("_presentPopoverForRevisionRange no commits", function()
{
    var element = document.createElement("div");
    var popover = new Dashboard.Popover();
    var context = {
        trac: this.trac,
        branch: this.trunkBranch,
        firstRevision: 33020,
        lastRevision: 33018
    };
    this.view._presentPopoverForRevisionRange(element, popover, context);
    var nodeList = popover._element.getElementsByClassName("pending-commit");
    strictEqual(nodeList.length, 0, "has 0 commits");
});

test("_revisionContentWithPopoverForIteration", function()
{
    var finished = false;
    var iteration = new BuildbotIteration(this.queue, 1, finished);
    iteration.revision = { "openSource": 33018 };
    var previousIteration = null;
    var content = this.view._revisionContentWithPopoverForIteration(iteration, previousIteration, this.trunkBranch);
    strictEqual(content.innerHTML, "r33018", "should have correct revision number.");
    strictEqual(content.classList.contains("revision-number"), true, "should have class 'revision-number'.");
    strictEqual(content.classList.contains("popover-tracking"), false, "should not have class 'popover-tracking'.");
});

test("_revisionContentWithPopoverForIteration has previousIteration", function()
{
    var finished = false;
    var iteration = new BuildbotIteration(this.queue, 2, finished);
    iteration.revision = { "openSource": 33022 };
    var previousIteration = new BuildbotIteration(this.queue, 1, finished);
    previousIteration.revision = { "openSource": 33018 };
    var content = this.view._revisionContentWithPopoverForIteration(iteration, previousIteration, this.trunkBranch);
    strictEqual(content.innerHTML, "r33022", "should have correct revision number.");
    strictEqual(content.classList.contains("revision-number"), true, "should have class 'revision-number'.");
    strictEqual(content.classList.contains("popover-tracking"), true, "should have class 'popover-tracking'.");
});

module("BuildBotQueue", {
    setup: function() {
        this.queue = new MockBuildbotQueue();
        this.queue.branches = [{
            name: "trunk",
            repository: {
                name: "openSource",
            }
        }];
    }
});

test("compareIterations by revisions", function()
{
    var finished = false;
    var iteration1 = new BuildbotIteration(this.queue, 1, finished);
    var iteration2 = new BuildbotIteration(this.queue, 2, finished);
    iteration1.revision = { "openSource": 33018 };
    iteration2.revision = { "openSource": 33019 };
    iteration1.loaded = true;
    iteration2.loaded = true;
    ok(this.queue.compareIterations(iteration2, iteration1) < 0, "compareIterations: less than");
    ok(this.queue.compareIterations(iteration1, iteration2) > 0, "compareIterations: greater than");
    strictEqual(this.queue.compareIterations(iteration2, iteration2), 0, "compareIterations: equal");
});

test("compareIterations by loaded (one revision missing)", function()
{
    var finished = false;
    var iteration1 = new BuildbotIteration(this.queue, 1, finished);
    var iteration2 = new BuildbotIteration(this.queue, 2, finished);
    iteration1.revision = {};
    iteration2.revision = { "openSource": 33019 };
    iteration1.loaded = false;
    iteration2.loaded = true;
    ok(this.queue.compareIterations(iteration1, iteration2) > 0, "compareIterations: greater than");
    ok(this.queue.compareIterations(iteration2, iteration1) < 0, "compareIterations: less than");
});

test("compareIterations by loaded (same revision)", function()
{
    var finished = false;
    var iteration1 = new BuildbotIteration(this.queue, 1, finished);
    var iteration2 = new BuildbotIteration(this.queue, 2, finished);
    iteration1.revision = { "openSource": 33019 };
    iteration2.revision = { "openSource": 33019 };
    iteration1.loaded = false;
    iteration2.loaded = true;
    ok(this.queue.compareIterations(iteration1, iteration2) > 0, "compareIterations: greater than");
    ok(this.queue.compareIterations(iteration2, iteration1) < 0, "compareIterations: less than");
});

test("compareIterations by id (revisions not specified)", function()
{
    var finished = false;
    var iteration1 = new BuildbotIteration(this.queue, 1, finished);
    var iteration2 = new BuildbotIteration(this.queue, 2, finished);
    iteration1.revision = {};
    iteration2.revision = {};
    iteration1.loaded = false;
    iteration2.loaded = false;
    ok(this.queue.compareIterations(iteration2, iteration1) < 0, "compareIterations: less than");
    ok(this.queue.compareIterations(iteration1, iteration2) > 0, "compareIterations: greater than");
    strictEqual(this.queue.compareIterations(iteration2, iteration2), 0, "compareIterations: equal");
});

test("compareIterations by id (same revision)", function()
{
    var finished = false;
    var iteration1 = new BuildbotIteration(this.queue, 1, finished);
    var iteration2 = new BuildbotIteration(this.queue, 2, finished);
    iteration1.revision = { "openSource": 33019 };
    iteration2.revision = { "openSource": 33019 };
    iteration1.loaded = false;
    iteration2.loaded = false;
    ok(this.queue.compareIterations(iteration2, iteration1) < 0, "compareIterations: less than");
    ok(this.queue.compareIterations(iteration1, iteration2) > 0, "compareIterations: greater than");
    strictEqual(this.queue.compareIterations(iteration2, iteration2), 0, "compareIterations: equal");
});

test("compareIterationsByRevisions", function()
{
    var finished = false;
    var iteration1 = new BuildbotIteration(this.queue, 1, finished);
    var iteration2 = new BuildbotIteration(this.queue, 2, finished);
    iteration1.revision = { "openSource": 33018 };
    iteration2.revision = { "openSource": 33019 };
    iteration1.loaded = true;
    iteration2.loaded = false;
    ok(this.queue.compareIterationsByRevisions(iteration2, iteration1) < 0, "compareIterationsByRevisions: less than");
    ok(this.queue.compareIterationsByRevisions(iteration1, iteration2) > 0, "compareIterationsByRevisions: greater than");
    strictEqual(this.queue.compareIterationsByRevisions(iteration2, iteration2), 0, "compareIterationsByRevisions: equal");
});