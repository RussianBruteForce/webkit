/*
 * Copyright (C) 2012 Adobe Systems Incorporated. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include "BasicShapes.h"

#include "BasicShapeFunctions.h"
#include "CalculationValue.h"
#include "FloatRect.h"
#include "FloatRoundedRect.h"
#include "LengthFunctions.h"
#include "Path.h"
#include "RenderBox.h"
#include "SVGPathByteStream.h"
#include "SVGPathUtilities.h"

namespace WebCore {

void BasicShapeCenterCoordinate::updateComputedLength()
{
    if (m_direction == TopLeft) {
        m_computedLength = m_length.isUndefined() ? Length(0, Fixed) : m_length;
        return;
    }
    if (m_length.isUndefined()) {
        m_computedLength = Length(100, Percent);
        return;
    }

    auto lhs = std::make_unique<CalcExpressionLength>(Length(100, Percent));
    auto rhs = std::make_unique<CalcExpressionLength>(m_length);
    auto op = std::make_unique<CalcExpressionBinaryOperation>(WTFMove(lhs), WTFMove(rhs), CalcSubtract);
    m_computedLength = Length(CalculationValue::create(WTFMove(op), CalculationRangeAll));
}

bool BasicShapeCircle::operator==(const BasicShape& other) const
{
    if (type() != other.type())
        return false;

    auto& otherCircle = downcast<BasicShapeCircle>(other);
    return m_centerX == otherCircle.m_centerX
        && m_centerY == otherCircle.m_centerY
        && m_radius == otherCircle.m_radius;
}

float BasicShapeCircle::floatValueForRadiusInBox(float boxWidth, float boxHeight) const
{
    if (m_radius.type() == BasicShapeRadius::Value)
        return floatValueForLength(m_radius.value(), sqrtf((boxWidth * boxWidth + boxHeight * boxHeight) / 2));

    float centerX = floatValueForCenterCoordinate(m_centerX, boxWidth);
    float centerY = floatValueForCenterCoordinate(m_centerY, boxHeight);

    float widthDelta = std::abs(boxWidth - centerX);
    float heightDelta = std::abs(boxHeight - centerY);
    if (m_radius.type() == BasicShapeRadius::ClosestSide)
        return std::min(std::min(std::abs(centerX), widthDelta), std::min(std::abs(centerY), heightDelta));

    // If radius.type() == BasicShapeRadius::FarthestSide.
    return std::max(std::max(std::abs(centerX), widthDelta), std::max(std::abs(centerY), heightDelta));
}

void BasicShapeCircle::path(Path& path, const FloatRect& boundingBox)
{
    ASSERT(path.isEmpty());

    float centerX = floatValueForCenterCoordinate(m_centerX, boundingBox.width());
    float centerY = floatValueForCenterCoordinate(m_centerY, boundingBox.height());
    float radius = floatValueForRadiusInBox(boundingBox.width(), boundingBox.height());
    path.addEllipse(FloatRect(
        centerX - radius + boundingBox.x(),
        centerY - radius + boundingBox.y(),
        radius * 2,
        radius * 2
    ));
}

bool BasicShapeCircle::canBlend(const BasicShape& other) const
{
    if (type() != other.type())
        return false;

    return radius().canBlend(downcast<BasicShapeCircle>(other).radius());
}

Ref<BasicShape> BasicShapeCircle::blend(const BasicShape& other, double progress) const
{
    ASSERT(type() == other.type());
    auto& otherCircle = downcast<BasicShapeCircle>(other);
    auto result =  BasicShapeCircle::create();

    result->setCenterX(m_centerX.blend(otherCircle.centerX(), progress));
    result->setCenterY(m_centerY.blend(otherCircle.centerY(), progress));
    result->setRadius(m_radius.blend(otherCircle.radius(), progress));
    return WTFMove(result);
}

bool BasicShapeEllipse::operator==(const BasicShape& other) const
{
    if (type() != other.type())
        return false;

    auto& otherEllipse = downcast<BasicShapeEllipse>(other);
    return m_centerX == otherEllipse.m_centerX
        && m_centerY == otherEllipse.m_centerY
        && m_radiusX == otherEllipse.m_radiusX
        && m_radiusY == otherEllipse.m_radiusY;
}

float BasicShapeEllipse::floatValueForRadiusInBox(const BasicShapeRadius& radius, float center, float boxWidthOrHeight) const
{
    if (radius.type() == BasicShapeRadius::Value)
        return floatValueForLength(radius.value(), std::abs(boxWidthOrHeight));

    float widthOrHeightDelta = std::abs(boxWidthOrHeight - center);
    if (radius.type() == BasicShapeRadius::ClosestSide)
        return std::min(std::abs(center), widthOrHeightDelta);

    ASSERT(radius.type() == BasicShapeRadius::FarthestSide);
    return std::max(std::abs(center), widthOrHeightDelta);
}

void BasicShapeEllipse::path(Path& path, const FloatRect& boundingBox)
{
    ASSERT(path.isEmpty());

    float centerX = floatValueForCenterCoordinate(m_centerX, boundingBox.width());
    float centerY = floatValueForCenterCoordinate(m_centerY, boundingBox.height());
    float radiusX = floatValueForRadiusInBox(m_radiusX, centerX, boundingBox.width());
    float radiusY = floatValueForRadiusInBox(m_radiusY, centerY, boundingBox.height());
    path.addEllipse(FloatRect(
        centerX - radiusX + boundingBox.x(),
        centerY - radiusY + boundingBox.y(),
        radiusX * 2,
        radiusY * 2));
}

bool BasicShapeEllipse::canBlend(const BasicShape& other) const
{
    if (type() != other.type())
        return false;

    auto& otherEllipse = downcast<BasicShapeEllipse>(other);
    return radiusX().canBlend(otherEllipse.radiusX()) && radiusY().canBlend(otherEllipse.radiusY());
}

Ref<BasicShape> BasicShapeEllipse::blend(const BasicShape& other, double progress) const
{
    ASSERT(type() == other.type());
    auto& otherEllipse = downcast<BasicShapeEllipse>(other);
    auto result = BasicShapeEllipse::create();

    if (m_radiusX.type() != BasicShapeRadius::Value || otherEllipse.radiusX().type() != BasicShapeRadius::Value
        || m_radiusY.type() != BasicShapeRadius::Value || otherEllipse.radiusY().type() != BasicShapeRadius::Value) {
        result->setCenterX(otherEllipse.centerX());
        result->setCenterY(otherEllipse.centerY());
        result->setRadiusX(otherEllipse.radiusX());
        result->setRadiusY(otherEllipse.radiusY());
        return WTFMove(result);
    }

    result->setCenterX(m_centerX.blend(otherEllipse.centerX(), progress));
    result->setCenterY(m_centerY.blend(otherEllipse.centerY(), progress));
    result->setRadiusX(m_radiusX.blend(otherEllipse.radiusX(), progress));
    result->setRadiusY(m_radiusY.blend(otherEllipse.radiusY(), progress));
    return WTFMove(result);
}

bool BasicShapePolygon::operator==(const BasicShape& other) const
{
    if (type() != other.type())
        return false;

    auto& otherPolygon = downcast<BasicShapePolygon>(other);
    return m_windRule == otherPolygon.m_windRule
        && m_values == otherPolygon.m_values;
}

void BasicShapePolygon::path(Path& path, const FloatRect& boundingBox)
{
    ASSERT(path.isEmpty());
    ASSERT(!(m_values.size() % 2));
    size_t length = m_values.size();
    
    if (!length)
        return;

    path.moveTo(FloatPoint(floatValueForLength(m_values.at(0), boundingBox.width()) + boundingBox.x(),
        floatValueForLength(m_values.at(1), boundingBox.height()) + boundingBox.y()));
    for (size_t i = 2; i < length; i = i + 2) {
        path.addLineTo(FloatPoint(floatValueForLength(m_values.at(i), boundingBox.width()) + boundingBox.x(),
            floatValueForLength(m_values.at(i + 1), boundingBox.height()) + boundingBox.y()));
    }
    path.closeSubpath();
}

bool BasicShapePolygon::canBlend(const BasicShape& other) const
{
    if (type() != other.type())
        return false;

    auto& otherPolygon = downcast<BasicShapePolygon>(other);
    return values().size() == otherPolygon.values().size() && windRule() == otherPolygon.windRule();
}

Ref<BasicShape> BasicShapePolygon::blend(const BasicShape& other, double progress) const
{
    ASSERT(type() == other.type());

    auto& otherPolygon = downcast<BasicShapePolygon>(other);
    ASSERT(m_values.size() == otherPolygon.values().size());
    ASSERT(!(m_values.size() % 2));

    size_t length = m_values.size();
    auto result = BasicShapePolygon::create();
    if (!length)
        return WTFMove(result);

    result->setWindRule(otherPolygon.windRule());

    for (size_t i = 0; i < length; i = i + 2) {
        result->appendPoint(m_values.at(i).blend(otherPolygon.values().at(i), progress),
            m_values.at(i + 1).blend(otherPolygon.values().at(i + 1), progress));
    }

    return WTFMove(result);
}

BasicShapePath::BasicShapePath(std::unique_ptr<SVGPathByteStream>&& byteStream)
    : m_byteStream(WTFMove(byteStream))
{
}

void BasicShapePath::path(Path& path, const FloatRect& boundingBox)
{
    ASSERT(path.isEmpty());
    buildPathFromByteStream(*m_byteStream, path);
    path.translate(toFloatSize(boundingBox.location()));
}

bool BasicShapePath::operator==(const BasicShape& other) const
{
    if (type() != other.type())
        return false;

    auto& otherPath = downcast<BasicShapePath>(other);
    return m_windRule == otherPath.m_windRule && *m_byteStream == *otherPath.m_byteStream;
}

bool BasicShapePath::canBlend(const BasicShape& other) const
{
    if (type() != other.type())
        return false;

    auto& otherPath = downcast<BasicShapePath>(other);
    return windRule() == otherPath.windRule() && canBlendSVGPathByteStreams(*m_byteStream, *otherPath.pathData());
}

Ref<BasicShape> BasicShapePath::blend(const BasicShape& from, double progress) const
{
    ASSERT(type() == from.type());

    auto& fromPath = downcast<BasicShapePath>(from);

    auto resultingPathBytes = std::make_unique<SVGPathByteStream>();
    buildAnimatedSVGPathByteStream(*fromPath.m_byteStream, *m_byteStream, *resultingPathBytes, progress);

    auto result = BasicShapePath::create(WTFMove(resultingPathBytes));
    result->setWindRule(windRule());
    return WTFMove(result);
}

bool BasicShapeInset::operator==(const BasicShape& other) const
{
    if (type() != other.type())
        return false;

    auto& otherInset = downcast<BasicShapeInset>(other);
    return m_right == otherInset.m_right
        && m_top == otherInset.m_top
        && m_bottom == otherInset.m_bottom
        && m_left == otherInset.m_left
        && m_topLeftRadius == otherInset.m_topLeftRadius
        && m_topRightRadius == otherInset.m_topRightRadius
        && m_bottomRightRadius == otherInset.m_bottomRightRadius
        && m_bottomLeftRadius == otherInset.m_bottomLeftRadius;
}

static FloatSize floatSizeForLengthSize(const LengthSize& lengthSize, const FloatRect& boundingBox)
{
    return FloatSize(floatValueForLength(lengthSize.width(), boundingBox.width()),
        floatValueForLength(lengthSize.height(), boundingBox.height()));
}

void BasicShapeInset::path(Path& path, const FloatRect& boundingBox)
{
    ASSERT(path.isEmpty());
    float left = floatValueForLength(m_left, boundingBox.width());
    float top = floatValueForLength(m_top, boundingBox.height());
    auto rect = FloatRect(left + boundingBox.x(), top + boundingBox.y(),
        std::max<float>(boundingBox.width() - left - floatValueForLength(m_right, boundingBox.width()), 0),
        std::max<float>(boundingBox.height() - top - floatValueForLength(m_bottom, boundingBox.height()), 0));
    auto radii = FloatRoundedRect::Radii(floatSizeForLengthSize(m_topLeftRadius, boundingBox),
        floatSizeForLengthSize(m_topRightRadius, boundingBox),
        floatSizeForLengthSize(m_bottomLeftRadius, boundingBox),
        floatSizeForLengthSize(m_bottomRightRadius, boundingBox));
    radii.scale(calcBorderRadiiConstraintScaleFor(rect, radii));
    path.addRoundedRect(FloatRoundedRect(rect, radii));
}

bool BasicShapeInset::canBlend(const BasicShape& other) const
{
    return type() == other.type();
}

Ref<BasicShape> BasicShapeInset::blend(const BasicShape& other, double progress) const
{
    ASSERT(type() == other.type());

    auto& otherInset = downcast<BasicShapeInset>(other);
    auto result =  BasicShapeInset::create();
    result->setTop(m_top.blend(otherInset.top(), progress));
    result->setRight(m_right.blend(otherInset.right(), progress));
    result->setBottom(m_bottom.blend(otherInset.bottom(), progress));
    result->setLeft(m_left.blend(otherInset.left(), progress));

    result->setTopLeftRadius(m_topLeftRadius.blend(otherInset.topLeftRadius(), progress));
    result->setTopRightRadius(m_topRightRadius.blend(otherInset.topRightRadius(), progress));
    result->setBottomRightRadius(m_bottomRightRadius.blend(otherInset.bottomRightRadius(), progress));
    result->setBottomLeftRadius(m_bottomLeftRadius.blend(otherInset.bottomLeftRadius(), progress));

    return WTFMove(result);
}
}
