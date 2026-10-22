#pragma once

#include "Supermodel.h"
#include "FBO.h"
#include "AspectRatio.h"
#include "New3D/GLSLShader.h"

// This class just implements super sampling. Super sampling looks fantastic but is quite expensive.
// 8x and beyond values can start to eat ridiculous amounts of memory / gpu time, for less and less noticable returns
// 4x works and looks great
// values such as 3 are also possible, that works out 9 samples per pixel
// The algorithm is super simple, just add up all samples and divide by the number
// It also draws the image in its frame for the AspectRatio setting (SetPresentation): letterboxed, stretched, cropped,
// with curved edges

class SuperAA
{
public:
	SuperAA(int aaValue, CRTcolor CRTcolors, bool forceTarget = false);	// forceTarget: render into the FBO even at 1x (agent mode reads it)
	~SuperAA();

	void Init(int width, int height);		// width & height are real window dimensions (with a presentation, the image's frame buffer)
	void SetPresentation(const AspectRatio::Layout *layout, int windowWidth, int windowHeight);	// before Init; nullptr: none
	void Draw();							// this is a no-op if AA is 1 and CRTcolors 0, since we'll be drawing straight on the back buffer anyway

	GLuint GetTargetID();
	GLuint GetWindowTargetID();				// the presentation's own frame buffer (forceTarget, for the agent), 0 if none

private:
	bool UsesTarget() const;
	void CreateShader();

	FBO m_fbo;
	GLSLShader m_shader;
	const int m_aa;
	const CRTcolor m_crtcolors;
	const bool m_forceTarget;
	GLuint m_vao;
	int m_width;
	int m_height;

	// The AspectRatio setting's final pass (forceTarget: into its own frame buffer, which stays bound for what is drawn
	// over it, so that the agent can read the window's image)
	FBO m_windowFbo;
	bool m_present;
	AspectRatio::Layout m_layout;
	int m_windowWidth;
	int m_windowHeight;
	GLint m_locPresent, m_locWindowSize, m_locFrame, m_locImage, m_locImageInTarget, m_locBarrelAxis, m_locBarrel1, m_locBarrel2, m_locSamples;
};
