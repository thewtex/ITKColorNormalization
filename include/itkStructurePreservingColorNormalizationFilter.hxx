/*=========================================================================
 *
 *  Copyright NumFOCUS
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

#ifndef itkStructurePreservingColorNormalizationFilter_hxx
#define itkStructurePreservingColorNormalizationFilter_hxx

#include "itkStructurePreservingColorNormalizationFilter.h"
#include "itkMersenneTwisterRandomVariateGenerator.h"
#include <numeric>

namespace itk
{

template< typename TImage >
StructurePreservingColorNormalizationFilter< TImage >
::StructurePreservingColorNormalizationFilter()
  : m_NumberOfDimensions( Self::PixelHelper< PixelType >::NumberOfDimensions ),
    m_NumberOfColors( Self::PixelHelper< PixelType >::NumberOfColors ),
    m_ColorIndexSuppressedByHematoxylin( Self::PixelHelper< PixelType >::ColorIndexSuppressedByHematoxylin ),
    m_ColorIndexSuppressedByEosin( Self::PixelHelper< PixelType >::ColorIndexSuppressedByEosin )
{
  // The number of colors had better be at least 3 or be unknown
  // (which is indicated with the value -1).
  static_assert( 2 / PixelHelper< PixelType >::NumberOfColors < 1, "Images need at least 3 colors" );
}


template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::PrintSelf( std::ostream & os, Indent indent ) const
{
  Superclass::PrintSelf( os, indent );

  os << indent << "ColorIndexSuppressedByHematoxylin: " << m_ColorIndexSuppressedByHematoxylin << std::endl
     << indent << "ColorIndexSuppressedByEosin: " << m_ColorIndexSuppressedByEosin << std::endl;
}


template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::GenerateInputRequestedRegion()
{
  // Call the superclass' implementation of this method
  Superclass::GenerateInputRequestedRegion();

  // Get pointers to the input image ( to be normalized ) and
  // reference image.
  ImageType *inputPtr = const_cast< ImageType * >( this->GetInput( 0 ) );
  ImageType *referPtr = const_cast< ImageType * >( this->GetInput( 1 ) );

  if( inputPtr != nullptr )
    {
    inputPtr->SetRequestedRegionToLargestPossibleRegion();
    }

  if( referPtr != nullptr )
    {
    referPtr->SetRequestedRegionToLargestPossibleRegion();
    }
}


template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::BeforeThreadedGenerateData()
{
  // Call the superclass' implementation of this method
  Superclass::BeforeThreadedGenerateData();

  // this->Modified() is called if a itkSetMacro is invoked, but not
  // if a this->GetInput() value is changed, right?!!!  Otherwise, we
  // need to implement our own set methods to update a separate MTime
  // variable.
  if( this->GetMTime() > m_ParametersMTime )
    {
    // m_ColorIndexSuppressedByHematoxylin and/or
    // m_ColorIndexSuppressedByEosin has changed since we built the
    // cache, so clear the cache.  The empty cache is current as of
    // the most recent modification.
    m_inputPtr = nullptr;
    m_referPtr = nullptr;
    m_ParametersMTime = this->GetMTime();
    }

  // If the pixel type is RGBPixel<T> or RGBAPixel<T> then
  // Self::PixelHelper has already provided values for
  // m_ColorIndexSuppressedByHematoxylin and
  // m_ColorIndexSuppressedByEosin.  Otherwise, the user must set them
  // directly.  Check that they have been set.
  itkAssertOrThrowMacro( m_ColorIndexSuppressedByHematoxylin >= 0 && m_ColorIndexSuppressedByEosin >= 0,
    "Need to set ColorIndexSuppressedByHematoxylin and ColorIndexSuppressedByEosin before using StructurePreservingColorNormalizationFilter" );

  // Find input and refer and make iterators for them.
  const ImageType * const inputPtr = this->GetInput( 0 ); // image to be normalized
  const ImageType * const referPtr = this->GetInput( 1 ); // reference image
  // For each of the two images, make sure that it was supplied, or
  // that we have it cached already.
  itkAssertOrThrowMacro( inputPtr != nullptr || m_inputPtr != nullptr, "An image to be normalized needs to be supplied as input image #0" );
  itkAssertOrThrowMacro( referPtr != nullptr || m_referPtr != nullptr, "A reference image needs to be supplied as input image #1" );

  // For each image, if there is a supplied image and it is different
  // from what we have cached then compute stuff and cache the
  // results.  These two calls to ImageToNMF could be done
  // simultaneously.
  if( inputPtr != nullptr && ( inputPtr != m_inputPtr || inputPtr->GetTimeStamp() != m_inputTimeStamp ) )
    {
    RegionConstIterator inputIter {inputPtr, inputPtr->GetRequestedRegion()};
    // A runtime check for number of colors is needed for a
    // VectorImage.
    if /*constexpr*/( Self::PixelHelper< PixelType >::NumberOfDimensions < 0 )
      {
      inputIter.GoToBegin();
      m_NumberOfDimensions = inputIter.Get().Size();
      m_NumberOfColors = m_NumberOfDimensions;
      itkAssertOrThrowMacro( m_NumberOfColors >= 3, "Images need at least 3 colors but the input image to be normalized does not" );
      if( referPtr == nullptr || ( referPtr == m_referPtr && referPtr->GetTimeStamp() == m_referTimeStamp ) )
        {
        RegionConstIterator referIter {m_referPtr, m_referPtr->GetRequestedRegion()};
        referIter.GoToBegin();
        itkAssertOrThrowMacro( m_NumberOfColors == referIter.Get().Size(),
          "The (cached) reference image needs its number of colors to be exactly the same as the input image to be normalized" );
        }
      }

    m_inputUnstainedPixel = Self::PixelHelper< PixelType >::pixelForColorsOnly( m_NumberOfColors );

    if( this->ImageToNMF( inputIter, m_inputH, m_inputUnstainedPixel ) == 0 )
      {
      m_inputPtr = inputPtr;
      m_inputTimeStamp = inputPtr->GetTimeStamp();
      }
    else
      {
      // we failed
      m_inputPtr = nullptr;
      itkAssertOrThrowMacro( m_inputPtr != nullptr, "The image to be normalized could not be processed; does it have white, blue, and pink pixels?" )
      }
    }

  if( referPtr != nullptr && ( referPtr != m_referPtr || referPtr->GetTimeStamp() != m_referTimeStamp ) )
    {
    // For VectorImage, check that number of colors is right in the
    // newly supplied reference image
    RegionConstIterator referIter {referPtr, referPtr->GetRequestedRegion()};
    if /*constexpr*/( Self::PixelHelper< PixelType >::NumberOfDimensions < 0 )
      {
      referIter.GoToBegin();
      itkAssertOrThrowMacro( m_NumberOfColors == referIter.Get().Size(),
        "The reference image needs its number of colors to be exactly the same as the input image to be normalized" );
      }
    m_referUnstainedPixel = Self::PixelHelper< PixelType >::pixelForColorsOnly( m_NumberOfColors );
    if( this->ImageToNMF( referIter, m_referH, m_referUnstainedPixel ) == 0 )
      {
      m_referPtr = referPtr;
      m_referTimeStamp = referPtr->GetTimeStamp();
      }
    else
      {
      // we failed
      m_referPtr = nullptr;
      itkAssertOrThrowMacro( m_referPtr != nullptr, "The reference image could not be processed; does it have white, blue, and pink pixels?" )
      }
    }

  if( ( m_inputH * m_referH.transpose() ).determinant() < CalcElementType( 0 ) )
    {
    // Somehow the hematoxylin and eosin rows got swapped in one of
    // the input image or reference image.  Flip them in referH to get
    // them in synch.
    static_assert( NumberOfStains == 2, "There must be exactly two stains" );
    const CalcMatrixType referHOriginal {m_referH};
    m_referH.row( 0 ) = referHOriginal.row( 1 );
    m_referH.row( 1 ) = referHOriginal.row( 0 );
    }

  itkAssertOrThrowMacro( ( m_inputH * m_referH.transpose() ).determinant() > CalcElementType( 0 ), "Hematoxylin and Eosin are getting mixed up; failed" );
}


template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::DynamicThreadedGenerateData( const RegionType & outputRegion )
{
  ImageType * const outputPtr = this->GetOutput();
  itkAssertOrThrowMacro( outputPtr != nullptr, "An output image needs to be supplied" )
  RegionIterator outputIter {outputPtr, outputRegion};

  this->NMFsToImage( m_inputH, m_inputUnstainedPixel, m_referH, m_referUnstainedPixel, outputIter );
}


template< typename TImage >
int
StructurePreservingColorNormalizationFilter< TImage >
::ImageToNMF( RegionConstIterator &inIter, CalcMatrixType &matrixH, PixelTypeForColorsOnly &unstainedPixel ) const
{
  // To maintain locality of memory references, we are using
  // numberOfPixels as the number of rows rather than as the number of
  // columns in row-major storage.  With V=WH, as is standard in
  // non-negative matrix factorization, our matrices switch names and
  // are transposed with respect to the Vahadane article.  In
  // particular, our W is a very tall matrix and our H is a fairly
  // compact matrix, whereas in Vahadane W is a fairly compact matrix
  // and H is a very wide matrix.

  const SizeType size = inIter.GetRegion().GetSize();
  const SizeValueType numberOfPixels = std::accumulate( size.begin(), size.end(), 1, std::multiplies< SizeValueType >() );

  // Find distinguishers.  These are essentially the rows of matrixH.
  CalcMatrixType distinguishers;
  CalcMatrixType matrixBrightV;
  CalcMatrixType matrixDarkV;

  this->ImageToMatrix( inIter, numberOfPixels, matrixBrightV, matrixDarkV );
  this->MatrixToDistinguishers( matrixBrightV, distinguishers );

  // Use the distinguishers as seeds to the non-negative matrix
  // factorization.
  if( this->DistinguishersToNMFSeeds( distinguishers, unstainedPixel, matrixH ) != 0 )
    {
    return 1;                   // we failed.
    }

  // Improve matrixH using Virtanen's algorithm
  { std::ostringstream mesg; mesg << "matrixH before VirtanenEuclidean = " << std::endl << matrixH << std::endl; std::cout << mesg.str() << std::flush; }
    {
    CalcMatrixType matrixW;     // Could end up large.
    this->VirtanenEuclidean( matrixBrightV, matrixW, matrixH );
    }

  // Rescale each row of matrixH so that the
  // (100-VeryDarkPercentileLevel) value of each column of matrixW is
  // 1.0.
  { std::ostringstream mesg; mesg << "matrixH before NormalizeMatrixH = " << std::endl << matrixH << std::endl; std::cout << mesg.str() << std::flush; }
  this->NormalizeMatrixH( matrixDarkV, unstainedPixel, matrixH );
  { std::ostringstream mesg; mesg << "matrixH at end = " << std::endl << matrixH << std::endl; std::cout << mesg.str() << std::flush; }

  return 0;
}


template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::ImageToMatrix( RegionConstIterator &inIter, SizeValueType numberOfPixels, CalcMatrixType &matrixBrightV, CalcMatrixType &matrixDarkV ) const
{
  // If the image is big, take a random subset of its pixels and put them into matrixV.
  using UniformGeneratorType = itk::Statistics::MersenneTwisterRandomVariateGenerator;
  UniformGeneratorType::Pointer uniformGenerator = UniformGeneratorType::New();
  uniformGenerator->Initialize( 20200609 );

  SizeValueType numberOfRows = std::min( numberOfPixels, maxNumberOfRows );

  CalcMatrixType matrixV {numberOfRows, m_NumberOfColors};
  for( inIter.GoToBegin(); !inIter.IsAtEnd(); ++inIter )
    {
    if ( uniformGenerator->GetVariate() * numberOfPixels-- < numberOfRows )
      {
      --numberOfRows;
      PixelType pixelValue = inIter.Get();
      for( Eigen::Index color = 0; color < m_NumberOfColors; ++color )
        {
        matrixV( numberOfRows, color ) = pixelValue[color];
        }
      }
    }

  // We do not want trouble with a value near zero ( when we take its
  // logarithm ) so we add a little to each value now.
  const CalcElementType nearZero {matrixV.lpNorm< Eigen::Infinity >() * epsilon1};
  matrixV = ( matrixV.array() + nearZero ).matrix();

  // Of the randomly chosen pixels, keep only those that are bright
  // enough or dark enough to be useful.
  this->MatrixToMatrixExtremes( matrixV, matrixBrightV, matrixDarkV );
}


// static method
template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::MatrixToMatrixExtremes( const CalcMatrixType &matrixV, CalcMatrixType &matrixBrightV, CalcMatrixType &matrixDarkV )
{
  const CalcColVectorType intensityOfPixels {matrixV.rowwise().sum()};

  // For finding the brightest pixels, find the specified percentile
  // threshold.
  CalcColVectorType brightRearranged {intensityOfPixels};
  SizeValueType const BrightPercentilePosition {static_cast< SizeValueType >( ( brightRearranged.size() - 1 ) * BrightPercentileLevel )};
  std::nth_element( Self::begin( brightRearranged ), Self::begin( brightRearranged ) + BrightPercentilePosition, Self::end( brightRearranged ) );
  const CalcElementType BrightPercentileThreshold {brightRearranged( BrightPercentilePosition )};

  // For finding the brightest pixels, find specified fraction of
  // maximum bright.
  const CalcElementType BrightPercentageThreshold {BrightPercentageLevel * *std::max_element( Self::cbegin( intensityOfPixels ), Self::cend( intensityOfPixels ) )};

  // For finding the brightest pixels, we will keep those pixels that
  // pass at least one of the above bright thresholds.
  const CalcElementType brightThreshold {std::min( BrightPercentileThreshold, BrightPercentageThreshold )};

  // For finding the darkest pixels, find the specified percentile
  // threshold.
  CalcColVectorType darkRearranged {intensityOfPixels};
  SizeValueType const DarkPercentilePosition {static_cast< SizeValueType >( ( darkRearranged.size() - 1 ) * DarkPercentileLevel )};
  std::nth_element( Self::begin( darkRearranged ), Self::begin( darkRearranged ) + DarkPercentilePosition, Self::end( darkRearranged ) );
  const CalcElementType DarkPercentileThreshold {darkRearranged( DarkPercentilePosition )};
  const CalcElementType darkThreshold {DarkPercentileThreshold};

  SizeValueType numberOfBrightRows {0};
  SizeValueType numberOfDarkRows {0};
  for( Eigen::Index i = 0 ; i < matrixV.rows(); ++i )
    {
    if( intensityOfPixels( i ) >= brightThreshold )
      {
      ++numberOfBrightRows;
      }
    if( intensityOfPixels( i ) <= darkThreshold )
      {
      ++numberOfDarkRows;
      }
    }
  matrixBrightV = CalcMatrixType {numberOfBrightRows, matrixV.cols()};
  numberOfBrightRows = 0;
  matrixDarkV = CalcMatrixType {numberOfDarkRows, matrixV.cols()};
  numberOfDarkRows = 0;
  for( Eigen::Index i = 0 ; i < matrixV.rows(); ++i )
    {
    if( intensityOfPixels( i ) >= brightThreshold )
      {
      matrixBrightV.row( numberOfBrightRows++ ) = matrixV.row( i );
      }
    if( intensityOfPixels( i ) <= darkThreshold )
      {
      matrixDarkV.row( numberOfDarkRows++ ) = matrixV.row( i );
      }
    }
}


// static method
template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::MatrixToDistinguishers( const CalcMatrixType &matrixV, CalcMatrixType &distinguishers )
{
  const CalcMatrixType normVStart {matrixV};

  // We will store the row ( pixel ) index of each distinguishing
  // pixel in firstPassDistinguisherIndices.
  std::array< int, NumberOfStains+1 > firstPassDistinguisherIndices {-1};
  SizeValueType numberOfDistinguishers {0};
  Self::FirstPassDistinguishers( normVStart, firstPassDistinguisherIndices, numberOfDistinguishers );

  // Each row of secondPassDistinguisherColors is the vector of color
  // values for a distinguisher.
  CalcMatrixType secondPassDistinguisherColors {numberOfDistinguishers, matrixV.cols()};
  Self::SecondPassDistinguishers( normVStart, firstPassDistinguisherIndices, numberOfDistinguishers, secondPassDistinguisherColors );

  distinguishers = secondPassDistinguisherColors;
}


// static method
template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::FirstPassDistinguishers( const CalcMatrixType &normVStart, std::array< int, NumberOfStains+1 > &firstPassDistinguisherIndices, SizeValueType &numberOfDistinguishers )
{
  CalcMatrixType normV {normVStart};
  numberOfDistinguishers = 0;
  bool needToRecenterMatrix = true;
  while( numberOfDistinguishers <= NumberOfStains )
    {
    // Find the next distinguishing row ( pixel )
    firstPassDistinguisherIndices[numberOfDistinguishers] = Self::MatrixToOneDistinguisher( normV );
    // If we found a distinguisher and we have not yet found
    // NumberOfStains+1 of them, then look for the next distinguisher.
    if( firstPassDistinguisherIndices[numberOfDistinguishers] >= 0 )
      {
      // We just found a distinguisher
      ++numberOfDistinguishers;
      if( numberOfDistinguishers <= NumberOfStains )
        {
        // Prepare to look for the next distinguisher
        if( needToRecenterMatrix )
          {
          normV = Self::RecenterMatrix( normV, firstPassDistinguisherIndices[numberOfDistinguishers - 1] );
          needToRecenterMatrix = false;
          }
        else
          {
          normV = Self::ProjectMatrix( normV, firstPassDistinguisherIndices[numberOfDistinguishers - 1] );
          }
        }
      }
    else
      {
      // We did not find another distinguisher.  There are no more.
      break;
      }
    }
}


// static method
template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::SecondPassDistinguishers( const CalcMatrixType &normVStart, const std::array< int, NumberOfStains+1 > &firstPassDistinguisherIndices, const SizeValueType numberOfDistinguishers,
  CalcMatrixType &secondPassDistinguisherColors )
{
  for( int distinguisher {0}; distinguisher < numberOfDistinguishers; ++distinguisher )
    {
    CalcMatrixType normV {normVStart};
    bool needToRecenterMatrix = true;
    for( int otherDistinguisher {0}; otherDistinguisher < numberOfDistinguishers; ++otherDistinguisher )
      {
      // skip if self
      if( otherDistinguisher != distinguisher )
        {
        if( needToRecenterMatrix )
          {
          normV = Self::RecenterMatrix( normV, firstPassDistinguisherIndices[otherDistinguisher] );
          needToRecenterMatrix = false;
          }
        else
          {
          normV = Self::ProjectMatrix( normV, firstPassDistinguisherIndices[otherDistinguisher] );
          }
        }
      }
    // We have sent all distinguishers except self to the origin.
    // Whatever is far from the origin in the same direction as self
    // is a good replacement for self.  We will take an average among
    // those that are at least 80% as far as the best.  ( Note that
    // self could still be best, but not always. )
    const CalcColVectorType dotProducts {normV * normV.row( firstPassDistinguisherIndices[distinguisher] ).transpose()};
    const CalcElementType threshold {*std::max_element( Self::cbegin( dotProducts ), Self::cend( dotProducts ) ) * 999 / 1000};
    CalcRowVectorType cumulative {CalcRowVectorType::Constant( 1, normVStart.cols(), 0.0 )};
    SizeValueType numberOfContributions {0};
    for( Eigen::Index row = 0; row < dotProducts.size(); ++row )
      {
      if( dotProducts( row ) >= threshold )
        {
        cumulative += normVStart.row( row );
        ++numberOfContributions;
        }
      }
    secondPassDistinguisherColors.row( distinguisher ) = cumulative / numberOfContributions;
    }
}


// static method
template< typename TImage >
int
StructurePreservingColorNormalizationFilter< TImage >
::MatrixToOneDistinguisher( const CalcMatrixType &normV )
{
  const CalcColVectorType lengths2 = normV.rowwise().squaredNorm();
  const CalcElementType * const result {std::max_element( Self::cbegin( lengths2 ), Self::cend( lengths2 ) )};
  if( *result > epsilon2 )
    {
    return std::distance( Self::cbegin( lengths2 ), result );
    }
  else
    {
    return -1;                // Nothing left to find
    }
}


// static method
template< typename TImage >
typename StructurePreservingColorNormalizationFilter< TImage >::CalcMatrixType
StructurePreservingColorNormalizationFilter< TImage >
::RecenterMatrix( const CalcMatrixType &normV, const SizeValueType row )
{
  return CalcMatrixType( normV.rowwise() - normV.row ( row ) );
}


// static method
template< typename TImage >
typename StructurePreservingColorNormalizationFilter< TImage >::CalcMatrixType
StructurePreservingColorNormalizationFilter< TImage >
::ProjectMatrix( const CalcMatrixType &normV, const SizeValueType row )
{
  const CalcRowVectorType rowValue {normV.row( row )};
  return normV - ( normV * rowValue.transpose() ) * ( rowValue / rowValue.squaredNorm() );
}


template< typename TImage >
int
StructurePreservingColorNormalizationFilter< TImage >
::DistinguishersToNMFSeeds( const CalcMatrixType &distinguishers, PixelTypeForColorsOnly &unstainedPixel, CalcMatrixType &matrixH ) const
{
  matrixH = CalcMatrixType {NumberOfStains, m_NumberOfColors};

  SizeValueType unstainedIndex;
  SizeValueType hematoxylinIndex;
  SizeValueType eosinIndex;
  this->DistinguishersToColors( distinguishers, unstainedIndex, hematoxylinIndex, eosinIndex );

  // If the indices unstainedIndex, hematoxylinIndex, and eosinIndex
  // are distinct then we choose a smart starting place for the
  // generic NMF algorithm.  Otherwise, we go with a guess that is
  // reasonable.
  if( unstainedIndex == hematoxylinIndex || unstainedIndex == eosinIndex || hematoxylinIndex == eosinIndex )
    {
    return 1;                   // we failed
    }

  const CalcRowVectorType unstainedCalcPixel {distinguishers.row( unstainedIndex )};
  for( Eigen::Index color = 0; color < m_NumberOfColors; ++ color )
    {
    unstainedPixel[color] = unstainedCalcPixel( color ); // return value
    }
  const CalcRowVectorType logUnstained {unstainedCalcPixel.unaryExpr( CalcUnaryFunctionPointer( std::log ) )};
  const CalcRowVectorType logHematoxylin {logUnstained - distinguishers.row( hematoxylinIndex ).unaryExpr( CalcUnaryFunctionPointer( std::log ) )};
  const CalcRowVectorType logEosin {logUnstained - distinguishers.row( eosinIndex ).unaryExpr( CalcUnaryFunctionPointer( std::log ) )};
  // Set rows of matrixH to reflect hematoxylin and eosin.
  matrixH.row( 0 ) = logHematoxylin;
  matrixH.row( 1 ) = logEosin;

  // If somehow an element of matrixH is negative, set it to zero.
  const auto clip = [] ( const CalcElementType &x )
    {
    return std::max( CalcElementType( 0.0 ), x );
    };
  matrixH = matrixH.unaryExpr( clip );

  return 0;
}


template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::DistinguishersToColors( CalcMatrixType const &distinguishers, SizeValueType &unstainedIndex, SizeValueType &hematoxylinIndex, SizeValueType &eosinIndex ) const
{
  // Figure out which, distinguishers are unstained ( highest
  // brightness ), hematoxylin ( suppresses red ), and eosin (
  // suppresses green ).
  const CalcColVectorType lengths2 {distinguishers.rowwise().squaredNorm()};
  const CalcElementType * const unstainedIterator {std::max_element( Self::cbegin( lengths2 ), Self::cend( lengths2 ) )};
  unstainedIndex =  std::distance( Self::cbegin( lengths2 ), unstainedIterator );
  // For typename RGBPixel, red is suppressed by hematoxylin and green
  // is suppressed by eosin.  More generally, the index of the color
  // suppressed by hematoxylin is indicated by
  // m_ColorIndexSuppressedByHematoxylin, and the index of the color
  // suppressed by eosin is indicated by
  // m_ColorIndexSuppressedByEosin.
  const CalcColVectorType redValues {distinguishers.col( m_ColorIndexSuppressedByHematoxylin )};
  const CalcElementType * const hematoxylinputIterator {std::min_element( Self::cbegin( redValues ), Self::cend( redValues ) )};
  hematoxylinIndex = std::distance( Self::cbegin( redValues ), hematoxylinputIterator );
  const CalcColVectorType greenValues {distinguishers.col( m_ColorIndexSuppressedByEosin )};
  const CalcElementType * const eosinputIterator {std::min_element( Self::cbegin( greenValues ), Self::cend( greenValues ) )};
  eosinIndex = std::distance( Self::cbegin( greenValues ), eosinputIterator );
}


template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::NormalizeMatrixH( const CalcMatrixType &matrixDarkVIn, const PixelTypeForColorsOnly &unstainedPixel, CalcMatrixType &matrixH ) const
{
  const CalcColVectorType firstOnes {CalcColVectorType::Constant( matrixDarkVIn.rows(), 1, 1.0 )};

  // Compute the VeryDarkPercentileLevel percentile of a stain's
  // negative(matrixW) column.  This a dark value due to its being the
  // (100 - VeryDarkPercentileLevel) among quantities of stain.
  CalcRowVectorType logUnstainedCalcPixel {m_NumberOfColors};
  for( Eigen::Index color = 0; color < m_NumberOfColors; ++color )
    {
    logUnstainedCalcPixel( color ) = std::log( static_cast< CalcElementType >( unstainedPixel[color] ) );
    }
  CalcMatrixType matrixDarkV {matrixDarkVIn};
  const CalcElementType nearZero {matrixDarkV.lpNorm< Eigen::Infinity >() * epsilon1};
  matrixDarkV = ( matrixDarkV.array() + nearZero ).matrix();
  matrixDarkV = ( firstOnes * logUnstainedCalcPixel) - matrixDarkV.unaryExpr( CalcUnaryFunctionPointer( std::log ) );

  const auto clip = [] ( const CalcElementType &x )
    {
    return std::max( CalcElementType( 0.0 ), x );
    };
  CalcMatrixType negativeMatrixW = ( ( matrixDarkV * matrixH.transpose() ).array() - lambda ).unaryExpr( clip ).matrix() * ( - matrixH * matrixH.transpose() ).inverse();
  for( Eigen::Index stain = 0; stain < NumberOfStains; ++stain )
    {
    CalcColVectorType columnW {negativeMatrixW.col( stain )};
    SizeValueType const VeryDarkPercentilePosition
      {static_cast< SizeValueType >( ( columnW.size() - 1 ) * VeryDarkPercentileLevel / DarkPercentileLevel )};
    std::nth_element( Self::begin( columnW ), Self::begin( columnW ) + VeryDarkPercentilePosition, Self::end( columnW ) );
    const CalcElementType VeryDarkPercentileThreshold {-columnW( VeryDarkPercentilePosition )};
    matrixH.row( stain ) *= VeryDarkPercentileThreshold;
    }
}


// static method
template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::VirtanenEuclidean( const CalcMatrixType &matrixV, CalcMatrixType &matrixW, CalcMatrixType &matrixH )
{
  const auto clip = [] ( const CalcElementType &x )
    {
    return std::max( CalcElementType( 0.0 ), x );
    };
  matrixW = ( ( ( ( matrixV * matrixH.transpose() ).array() - lambda ).unaryExpr( clip ) + epsilon2 ).matrix() * ( matrixH * matrixH.transpose() ).inverse() ).unaryExpr( clip );

  // Apply Virtanen's algorithm to iteratively improve matrixW and
  // matrixH.  Note that parentheses optimize the order of matrix
  // chain multiplications and affect the speed of this method.
  CalcMatrixType previousMatrixW {matrixW};
  for( SizeValueType loopIter {0}; loopIter < maxNumberOfIterations; ++loopIter )
    {
    // Lasso term "lambda" insertion is possibly in a novel way.
    matrixW = (
      matrixW.array()
      * ( ( ( ( matrixV * matrixH.transpose() ).array() - lambda ).unaryExpr( clip ) + epsilon2 )
        / ( ( matrixW * ( matrixH * matrixH.transpose() ) ).array() + epsilon2 ) ) ).matrix();
    matrixH = (
      matrixH.array()
      * ( ( ( matrixW.transpose() * matrixV ).array() + epsilon2 )
        / ( ( ( matrixW.transpose() * matrixW ) * matrixH ).array() + epsilon2 ) ) ).matrix();
    // In lieu of rigorous Lagrange multipliers, renormalize rows of
    // matrixH to have unit magnitude.
    matrixH = CalcRowVectorType( matrixH.rowwise().squaredNorm() ).unaryExpr( CalcUnaryFunctionPointer( std::sqrt ) ).asDiagonal().inverse() * matrixH;
    if( ( loopIter & 15 ) == 15 )
      {
      if( ( matrixW - previousMatrixW ).lpNorm< Eigen::Infinity >() < epsilon0 )
        {
        break;
        }
      previousMatrixW = matrixW;
      }
    }

  // Round off values in the response, so that numbers are quite small
  // are set to zero.
  const CalcElementType maxW = matrixW.lpNorm< Eigen::Infinity >() * 15;
  matrixW = ( ( matrixW.array() + maxW ) - maxW ).matrix();
}


// static method
template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::VirtanenKLDivergence( const CalcMatrixType &matrixV, CalcMatrixType &matrixW, CalcMatrixType &matrixH )
{
  // If this method is going to get used, we may need to incorporate
  // the Lasso penalty lambda for matrixW and incorporate the Lagrange
  // multipliers to make each row of matrixH have magnitude 1.0.

  const auto clip = [] ( const CalcElementType &x )
    {
    return std::max( CalcElementType( 0.0 ), x );
    };
  matrixW = ( ( ( ( matrixV * matrixH.transpose() ).array() - lambda ).unaryExpr( clip ) + epsilon2 ).matrix() * ( matrixH * matrixH.transpose() ).inverse() ).unaryExpr( clip );

  // Apply Virtanen's algorithm to iteratively improve matrixW and
  // matrixH.
  const CalcRowVectorType firstOnes {CalcRowVectorType::Constant( 1, matrixV.rows(), 1.0 )};
  const CalcColVectorType lastOnes {CalcColVectorType::Constant( matrixV.cols(), 1, 1.0 )};
  CalcMatrixType previousMatrixW {matrixW};
  for( SizeValueType loopIter {0}; loopIter < maxNumberOfIterations; ++loopIter )
    {
    matrixW = (
      matrixW.array()
      * ( ( ( ( ( matrixV.array() + epsilon2 ) / ( ( matrixW * matrixH ).array() + epsilon2 ) ).matrix() * matrixH.transpose() ).array() + epsilon2 )
        / ( ( ( matrixH.rowwise().sum() ) * firstOnes ).transpose().array() + epsilon2 ) ) ).matrix();
    matrixH = (
      matrixW.array()
      * ( ( ( matrixW.transpose() * ( ( matrixV.array() + epsilon2 ) / ( ( matrixW * matrixH ).array() + epsilon2 ) ).matrix() ).array() + epsilon2 )
        / ( ( lastOnes * ( matrixW.colwise().sum() ) ).transpose().array() + epsilon2 ) ) ).matrix();
    // In lieu of rigorous Lagrange multipliers, renormalize rows of
    // matrixH to have unit magnitude.
    matrixH = CalcRowVectorType( matrixH.rowwise().squaredNorm() ).unaryExpr( CalcUnaryFunctionPointer( std::sqrt ) ).asDiagonal().inverse() * matrixH;
    if( ( loopIter & 15 ) == 15 )
      {
      if( ( matrixW - previousMatrixW ).lpNorm< Eigen::Infinity >() < epsilon0 )
        break;
      previousMatrixW = matrixW;
      }
    }

  // Round off values in the response, so that numbers are quite small
  // are set to zero.
  const CalcElementType maxW = matrixW.lpNorm< Eigen::Infinity >() * 15;
  matrixW = ( ( matrixW.array() + maxW ) - maxW ).matrix();
}


template< typename TImage >
void
StructurePreservingColorNormalizationFilter< TImage >
::NMFsToImage( const CalcMatrixType &inputH, const PixelTypeForColorsOnly &inputUnstained, const CalcMatrixType &referH, const PixelTypeForColorsOnly &referUnstained,
  RegionIterator &outputIter ) const
{
  // Read in corresponding part of the input region.
  const SizeType size = outputIter.GetRegion().GetSize();
  const SizeValueType numberOfPixels = std::accumulate( size.begin(), size.end(), 1, std::multiplies< SizeValueType >() );
  CalcMatrixType matrixV {numberOfPixels, m_NumberOfColors};
  RegionConstIterator inputIter {m_inputPtr, m_inputPtr->GetRequestedRegion()};
  outputIter.GoToBegin();
  inputIter.GoToBegin();
  for( SizeValueType pixelIndex {0}; !outputIter.IsAtEnd(); ++outputIter, ++inputIter, ++pixelIndex )
    {
    // Find input index that matches this output index.
    while ( inputIter.GetIndex() != outputIter.GetIndex() )
      {
      ++inputIter;
      }
    PixelType pixelValue = inputIter.Get();
    for( Eigen::Index color = 0; color < m_NumberOfColors; ++color )
      {
      matrixV( pixelIndex, color ) = pixelValue[color];
      }
    }

  // Convert matrixV using the inputUnstained pixel and a call to
  // logarithm.
  CalcRowVectorType logInputUnstained {1, m_NumberOfColors};
  CalcRowVectorType logReferUnstained {1, m_NumberOfColors};
  for( Eigen::Index color = 0; color < m_NumberOfColors; ++color )
    {
    logInputUnstained[color] = std::log( CalcElementType( inputUnstained[color] ) );
    logReferUnstained[color] = std::log( CalcElementType( referUnstained[color] ) );
    }

    {
    const CalcElementType nearZero {matrixV.lpNorm< Eigen::Infinity >() * epsilon1};
    matrixV = ( matrixV.array() + nearZero ).matrix();
    }
  const CalcColVectorType firstOnes {CalcColVectorType::Constant( numberOfPixels, 1, 1.0 )};
  matrixV = ( firstOnes * logInputUnstained ) - matrixV.unaryExpr( CalcUnaryFunctionPointer( std::log ) );
  const auto clip = [] ( const CalcElementType &x )
    {
    return std::max( CalcElementType( 0.0 ), x );
    };
  matrixV = matrixV.unaryExpr( clip );
    {
    const CalcElementType nearZero {matrixV.lpNorm< Eigen::Infinity >() * epsilon1};
    matrixV = ( matrixV.array() + nearZero ).matrix();
    }

  // Find the associated matrixW
  CalcMatrixType matrixW = ( ( matrixV * inputH.transpose() ).array() - lambda ).matrix() * ( inputH * inputH.transpose() ).inverse();

  // Use the matrixW with referH to compute updated values for
  // matrixV.
  matrixV = matrixW * referH;

  // Convert matrixV using exponentiation and the referUnstained pixel.
  matrixV = ( ( firstOnes * logReferUnstained ) - matrixV ).unaryExpr( CalcUnaryFunctionPointer( std::exp ) );

  PixelType pixelValue = Self::PixelHelper< PixelType >::pixelOfAllDimensions( m_NumberOfDimensions );
  outputIter.GoToBegin();
  inputIter.GoToBegin();
  for( SizeValueType pixelIndex {0}; !outputIter.IsAtEnd(); ++outputIter, ++pixelIndex )
    {
    while ( inputIter.GetIndex() != outputIter.GetIndex() )
      {
      ++inputIter;
      }
    for( Eigen::Index color = 0; color < m_NumberOfColors; ++color )
      {
      pixelValue[color] = matrixV( pixelIndex, color );
      }
    const PixelType inputPixel = inputIter.Get();
    for( Eigen::Index dim = m_NumberOfColors; dim < m_NumberOfDimensions; ++dim )
      {
      pixelValue[dim] = inputPixel[dim];
      }
    outputIter.Set( pixelValue );
    }
}

#if STRUCTUREPRESERVINGCOLORNORMALIZATIONFILTER_STRICT_EIGEN3_ITERATORS
template< typename TImage >
template< typename _Scalar, int _Rows, int _Cols, int _Options, int _MaxRows, int _MaxCols >
_Scalar *
StructurePreservingColorNormalizationFilter< TImage >
::begin( Eigen::Matrix< _Scalar, _Rows, _Cols, _Options, _MaxRows, _MaxCols > &matrix )
{
  return matrix.data();
}

template< typename TImage >
template< typename _Scalar, int _Rows, int _Cols, int _Options, int _MaxRows, int _MaxCols >
const _Scalar *
StructurePreservingColorNormalizationFilter< TImage >
::cbegin( const Eigen::Matrix< _Scalar, _Rows, _Cols, _Options, _MaxRows, _MaxCols > &matrix )
{
  return matrix.data();
}

template< typename TImage >
template< typename _Scalar, int _Rows, int _Cols, int _Options, int _MaxRows, int _MaxCols >
_Scalar *
StructurePreservingColorNormalizationFilter< TImage >
::end( Eigen::Matrix< _Scalar, _Rows, _Cols, _Options, _MaxRows, _MaxCols > &matrix )
{
  return matrix.data() + matrix.size();
}

template< typename TImage >
template< typename _Scalar, int _Rows, int _Cols, int _Options, int _MaxRows, int _MaxCols >
const _Scalar *
StructurePreservingColorNormalizationFilter< TImage >
::cend( const Eigen::Matrix< _Scalar, _Rows, _Cols, _Options, _MaxRows, _MaxCols > &matrix )
{
  return matrix.data() + matrix.size();
}

#else
template< typename TImage >
template< typename TMatrix >
typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType *
StructurePreservingColorNormalizationFilter< TImage >
::begin( TMatrix &matrix )
{
  return matrix.data();
}

template< typename TImage >
template< typename TMatrix >
const typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType *
StructurePreservingColorNormalizationFilter< TImage >
::cbegin( const TMatrix &matrix )
{
  return matrix.data();
}

template< typename TImage >
template< typename TMatrix >
typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType *
StructurePreservingColorNormalizationFilter< TImage >
::end( TMatrix &matrix )
{
  return matrix.data() + matrix.size();
}

template< typename TImage >
template< typename TMatrix >
const typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType *
StructurePreservingColorNormalizationFilter< TImage >
::cend( const TMatrix &matrix )
{
  return matrix.data() + matrix.size();
}
#endif

// Several members that are declared static constexpr are used by
// reference, and some compilers will thus demand that they be defined
// too.  We do that here.

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::SizeValueType
StructurePreservingColorNormalizationFilter< TImage >
::NumberOfStains;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TImage >
::BrightPercentileLevel;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TImage >
::BrightPercentageLevel;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TImage >
::DarkPercentileLevel;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TImage >
::VeryDarkPercentileLevel;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TImage >
::epsilon0;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TImage >
::epsilon1;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TImage >
::epsilon2;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::CalcElementType
StructurePreservingColorNormalizationFilter< TImage >
::lambda;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::SizeValueType
StructurePreservingColorNormalizationFilter< TImage >
::maxNumberOfIterations;

template< typename TImage >
constexpr typename StructurePreservingColorNormalizationFilter< TImage >::SizeValueType
StructurePreservingColorNormalizationFilter< TImage >
::maxNumberOfRows;

} // end namespace itk

#endif // itkStructurePreservingColorNormalizationFilter_hxx
