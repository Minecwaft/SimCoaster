#pragma once
#include "sam_record.hpp"

namespace simcoaster {
struct CoasterMeshDefinition;

//Coaster.sam groups needed for resource lookup and train configuration.
//Unrelated dynamic-texture groups are not interpreted by this projection.
struct DynamicTexturePylonBalance {
    [[=sam::Field { "pcMeshFilename", sam::Kind::string }]] std::optional<std::string> pcMeshFilename {};
    [[=sam::Field { "uiPylonTypeUniqueID", sam::Kind::nonnegative_integer }]] std::int32_t uiPylonTypeUniqueID {};
    [[=sam::Field { "bDontDisableBaseOnStack", 0, 2 }]] std::int32_t bDontDisableBaseOnStack {};
    [[=sam::Field { "fCollisionPossibleRadius", sam::Kind::nonnegative_scalar }]] float fCollisionPossibleRadius {};
    [[=sam::Field { "fPylonStraightDotThreshold", sam::Kind::scalar }]] float fPylonStraightDotThreshold {};
    [[=sam::Field { "uiPylonWidthCells", 1, 9 }]] std::int32_t uiPylonWidthCells {};
    [[=sam::Field { "uiPylonLengthCells", 1, 9 }]] std::int32_t uiPylonLengthCells {};
    [[=sam::Field { "uiMaxStackedPylons", 0, 11 }]] std::int32_t uiMaxStackedPylons {};
    [[=sam::Field { "fMinHeight", sam::Kind::scalar }]] float fMinHeight {};
    [[=sam::Field { "fMaxHeight", sam::Kind::scalar }]] float fMaxHeight {};
    [[=sam::Field { "fMaxHeightDiffPerGSquareDist", sam::Kind::scalar }]] float fMaxHeightDiffPerGSquareDist {};
    [[=sam::Field { "fEntranceMinXZAngleAtMinDist", sam::Kind::scalar }]] float fEntranceMinXZAngleAtMinDist {};
    [[=sam::Field { "fEntranceMaxXZAngleAtMinDist", sam::Kind::scalar }]] float fEntranceMaxXZAngleAtMinDist {};
    [[=sam::Field { "fEntranceMinXZAngleAtMaxDist", sam::Kind::scalar }]] float fEntranceMinXZAngleAtMaxDist {};
    [[=sam::Field { "fEntranceMaxXZAngleAtMaxDist", sam::Kind::scalar }]] float fEntranceMaxXZAngleAtMaxDist {};
    [[=sam::Field { "fMinXZAngleAtMinDist", sam::Kind::scalar }]] float fMinXZAngleAtMinDist {};
    [[=sam::Field { "fMaxXZAngleAtMinDist", sam::Kind::scalar }]] float fMaxXZAngleAtMinDist {};
    [[=sam::Field { "fMinXZAngleAtMaxDist", sam::Kind::scalar }]] float fMinXZAngleAtMaxDist {};
    [[=sam::Field { "fMaxXZAngleAtMaxDist", sam::Kind::scalar }]] float fMaxXZAngleAtMaxDist {};
    [[=sam::Field { "fMaxRotnAngleDeviance", sam::Kind::scalar }]] float fMaxRotnAngleDeviance {};
    [[=sam::Field { "fMaxLoopRotnAngleDeviance", sam::Kind::scalar }]] float fMaxLoopRotnAngleDeviance {};
    [[=sam::Field { "uiMinPlaceDist", sam::Kind::nonnegative_integer }]] std::int32_t uiMinPlaceDist {};
    [[=sam::Field { "uiMaxPlaceDist", sam::Kind::nonnegative_integer }]] std::int32_t uiMaxPlaceDist {};
    [[=sam::Field { "fMaxLoopHeightDifference", sam::Kind::scalar }]] float fMaxLoopHeightDifference {};
    [[=sam::Field { "fCostPerUnit", sam::Kind::scalar }]] float fCostPerUnit {};
    [[=sam::Field { "fCostPer2DLength", sam::Kind::scalar }]] float fCostPer2DLength {};
    [[=sam::Field { "fCostPerHeight", sam::Kind::scalar }]] float fCostPerHeight {};
    [[=sam::Field { "fLoftAnimStartCap", sam::Kind::scalar }]] float fLoftAnimStartCap {};
    [[=sam::Field { "fLoftAnimEndCap", sam::Kind::scalar }]] float fLoftAnimEndCap {};
    [[=sam::Field { "fLoftAnimMultiplier", sam::Kind::scalar }]] float fLoftAnimMultiplier {};
    [[=sam::Field { "fLoftAnimAdd", sam::Kind::scalar }]] float fLoftAnimAdd {};
    [[=sam::Field { "fRotateAnimStartCap", sam::Kind::scalar }]] float fRotateAnimStartCap {};
    [[=sam::Field { "fRotateAnimEndCap", sam::Kind::scalar }]] float fRotateAnimEndCap {};
    [[=sam::Field { "fRotateAnimMultiplier", sam::Kind::scalar }]] float fRotateAnimMultiplier {};
    [[=sam::Field { "fRotateAnimAdd", sam::Kind::scalar }]] float fRotateAnimAdd {};
    [[=sam::Field { "fInclineAnimStartCap", sam::Kind::scalar }]] float fInclineAnimStartCap {};
    [[=sam::Field { "fInclineAnimEndCap", sam::Kind::scalar }]] float fInclineAnimEndCap {};
    [[=sam::Field { "fInclineAnimMultiplier", sam::Kind::scalar }]] float fInclineAnimMultiplier {};
    [[=sam::Field { "fInclineAnimAdd", sam::Kind::scalar }]] float fInclineAnimAdd {};
    [[=sam::Field { "fBankAnimStartCap", sam::Kind::scalar }]] float fBankAnimStartCap {};
    [[=sam::Field { "fBankAnimEndCap", sam::Kind::scalar }]] float fBankAnimEndCap {};
    [[=sam::Field { "fBankAnimMultiplier", sam::Kind::scalar }]] float fBankAnimMultiplier {};
    [[=sam::Field { "fBankAnimAdd", sam::Kind::scalar }]] float fBankAnimAdd {};
    [[=sam::Field { "fLoopRotateAnimStartCap", sam::Kind::scalar }]] float fLoopRotateAnimStartCap {};
    [[=sam::Field { "fLoopRotateAnimEndCap", sam::Kind::scalar }]] float fLoopRotateAnimEndCap {};
    [[=sam::Field { "fLoopRotateAnimMultiplier", sam::Kind::scalar }]] float fLoopRotateAnimMultiplier {};
    [[=sam::Field { "fLoopRotateAnimAdd", sam::Kind::scalar }]] float fLoopRotateAnimAdd {};
};

struct DynamicTextureCarBalance {
    [[=sam::Field { "bBendCar", 0, 2 }]] std::int32_t bBendCar {};
    [[=sam::Field { "uiCarTypeUniqueID", sam::Kind::nonnegative_integer }]] std::int32_t uiCarTypeUniqueID {};
    [[=sam::Field { "pcMeshFilename", sam::Kind::string }]] std::optional<std::string> pcMeshFilename {};
    [[=sam::Field { "fFrontDistanceAdd", sam::Kind::scalar }]] float fFrontDistanceAdd {};
    [[=sam::Field { "fRearDistanceAdd", sam::Kind::scalar }]] float fRearDistanceAdd {};
    [[=sam::Field { "fYOffset", sam::Kind::scalar }]] float fYOffset {};
    [[=sam::Field { "fAnimSpeedMultiplier", sam::Kind::scalar }]] float fAnimSpeedMultiplier {};
};

struct DynamicTextureTrainBalance {
    [[=sam::Field { "uiMaxCars", sam::Kind::nonnegative_integer }]] std::int32_t uiMaxCars {};
    [[=sam::Field { "uiFrontCarTypeIndex", 0, 3 }]] std::int32_t uiFrontCarTypeIndex {};
    [[=sam::Field { "uiCentreCarTypeIndex", 0, 3 }]] std::int32_t uiCentreCarTypeIndex {};
    [[=sam::Field { "uiRearCarTypeIndex", 0, 3 }]] std::int32_t uiRearCarTypeIndex {};
    [[=sam::Field { "fFrontCarSpacing", sam::Kind::scalar }]] float fFrontCarSpacing {};
    [[=sam::Field { "fStdCarSpacing", sam::Kind::scalar }]] float fStdCarSpacing {};
    [[=sam::Field { "fEndCarSpacing", sam::Kind::scalar }]] float fEndCarSpacing {};
};

struct DynamicTextureCoasterBalance {
    [[=sam::Field { "fCurviness", sam::Kind::nonnegative_scalar }]] float fCurviness {};
    [[=sam::Field { "fStraightDivisor", sam::Kind::nonnegative_scalar }]] float fStraightDivisor {};
    [[=sam::Field { "fAutoBankDivisor", sam::Kind::scalar }]] float fAutoBankDivisor {};
    [[=sam::Field { "fAccelerationPerHeight", sam::Kind::scalar }]] float fAccelerationPerHeight {};
    [[=sam::Field { "fUphillAccelModifier", sam::Kind::scalar }]] float fUphillAccelModifier {};
    [[=sam::Field { "fDownhillAccelModifier", sam::Kind::scalar }]] float fDownhillAccelModifier {};
    [[=sam::Field { "fInitialPylonLoftHeight", sam::Kind::scalar }]] float fInitialPylonLoftHeight {};
    [[=sam::Field { "uiMaxTrains", 0, 51 }]] std::int32_t uiMaxTrains {};
    [[=sam::Field { "uiTrainReleaseRate", sam::Kind::nonnegative_integer }]] std::int32_t uiTrainReleaseRate {};
    [[=sam::Field { "bSpecialIsTrough", 0, 2 }]] std::int32_t bSpecialIsTrough {};
    [[=sam::Field { "bIsSuspendedCoaster", 0, 2 }]] std::int32_t bIsSuspendedCoaster {};
    [[=sam::Field { "bNoAutoBanking", 0, 2 }]] std::int32_t bNoAutoBanking {};
    [[=sam::Field { "bVisibleEndPylon", 0, 2 }]] std::int32_t bVisibleEndPylon {};
    [[=sam::Field { "uiInvalidCrossSection", 0, 21 }]] std::int32_t uiInvalidCrossSection {};
    [[=sam::Field { "uiWinchCrossSection", 0, 21 }]] std::int32_t uiWinchCrossSection {};
    [[=sam::Field { "fTrackHeightAdjust", sam::Kind::scalar }]] float fTrackHeightAdjust {};
    [[=sam::Field { "fWinchSpeed", sam::Kind::scalar }]] float fWinchSpeed {};
    [[=sam::Field { "fMinSpeed", sam::Kind::scalar }]] float fMinSpeed {};
    [[=sam::Field { "fMaxSpeedAtMinSetting", sam::Kind::scalar }]] float fMaxSpeedAtMinSetting {};
    [[=sam::Field { "fMaxSpeedAtMaxSetting", sam::Kind::scalar }]] float fMaxSpeedAtMaxSetting {};
    [[=sam::Field { "fFrictionMultiplier", sam::Kind::scalar }]] float fFrictionMultiplier {};
    [[=sam::Field { "fBendElevationLow", sam::Kind::scalar }]] float fBendElevationLow {};
    [[=sam::Field { "fBendElevationHigh", sam::Kind::scalar }]] float fBendElevationHigh {};
    [[=sam::Field { "fGravityX", sam::Kind::scalar }]] float fGravityX {};
    [[=sam::Field { "fGravityY", sam::Kind::scalar }]] float fGravityY {};
    [[=sam::Field { "fGravityZ", sam::Kind::scalar }]] float fGravityZ {};
    [[=sam::Field { "fForceMultiplier", sam::Kind::scalar }]] float fForceMultiplier {};
    [[=sam::Field { "fInitialSickness", sam::Kind::scalar }]] float fInitialSickness {};
    [[=sam::Field { "fInitialExcitement", sam::Kind::scalar }]] float fInitialExcitement {};
    [[=sam::Field { "fInitialUncomfortableness", sam::Kind::scalar }]] float fInitialUncomfortableness {};
    [[=sam::Field { "fGForceSicknessModifier", sam::Kind::scalar }]] float fGForceSicknessModifier {};
    [[=sam::Field { "fUpsideDownSicknessModifier", sam::Kind::scalar }]] float fUpsideDownSicknessModifier {};
    [[=sam::Field { "fSpeedExcitementModifier", sam::Kind::scalar }]] float fSpeedExcitementModifier {};
    [[=sam::Field { "fGForceExcitementModifier", sam::Kind::scalar }]] float fGForceExcitementModifier {};
    [[=sam::Field { "fUpsideDownExcitementModifier", sam::Kind::scalar }]] float fUpsideDownExcitementModifier {};
    [[=sam::Field { "fBankExcitementModifier", sam::Kind::scalar }]] float fBankExcitementModifier {};
    [[=sam::Field { "fXGForceUncomfortableModifier", sam::Kind::scalar }]] float fXGForceUncomfortableModifier {};
    [[=sam::Field { "fInvertedUncomfortableModifier", sam::Kind::scalar }]] float fInvertedUncomfortableModifier {};
    [[=sam::Field { "fNormalScreamStartExciteDelta", sam::Kind::scalar }]] float fNormalScreamStartExciteDelta {};
    [[=sam::Field { "fTerrorScreamStartExciteDelta", sam::Kind::scalar }]] float fTerrorScreamStartExciteDelta {};
    [[=sam::Field { "fNormalScreamSustainExcitement", sam::Kind::scalar }]] float fNormalScreamSustainExcitement {};
    [[=sam::Field { "fTerrorScreamSustainExcitement", sam::Kind::scalar }]] float fTerrorScreamSustainExcitement {};
    [[=sam::Field { "fEntranceSizeInterval", sam::Kind::scalar }]] float fEntranceSizeInterval {};
    [[=sam::Field { "iTrackSectionMeshVerts", 0, 1001 }]] std::int32_t iTrackSectionMeshVerts {};
    [[=sam::Field { "iTrackSectionMeshFaces", 0, 1001 }]] std::int32_t iTrackSectionMeshFaces {};
};

struct DynamicTextureMeshBalance {
    std::shared_ptr<const CoasterMeshDefinition> mesh_definition;
    [[=sam::Group { "sTrainType" }]] DynamicTextureTrainBalance train {};
    [[=sam::Group { "sCoasterType" }]] DynamicTextureCoasterBalance coaster {};
    [[=sam::Rows { "asPylonControls" }]] std::array<DynamicTexturePylonBalance, 2> pylons {};
    [[=sam::Extent { "asPylonControls" }]] std::uint32_t pylon_count {};
    [[=sam::Rows { "asCarTypes" }]] std::array<DynamicTextureCarBalance, 3> cars {};
    [[=sam::Extent { "asCarTypes" }]] std::uint32_t car_count {};
};
} //namespace simcoaster
