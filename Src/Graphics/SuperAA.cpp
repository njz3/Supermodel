#include "SuperAA.h"
#include <algorithm>
#include <string>

SuperAA::SuperAA(int aaValue, CRTcolor CRTcolors, bool forceTarget, bool depthStencil) :
	m_aa(aaValue),
	m_crtcolors(CRTcolors),
	m_forceTarget(forceTarget),
	m_depthStencil(depthStencil),
	m_vao(0),
	m_width(0),
	m_height(0),
	m_present(false),
	m_layout{},
	m_windowWidth(0),
	m_windowHeight(0),
	m_locPresent(-1), m_locWindowSize(-1), m_locFrame(-1), m_locImage(-1), m_locImageInTarget(-1),
	m_locBarrelAxis(-1), m_locBarrel1(-1), m_locBarrel2(-1), m_locSamples(-1)
{
}

// The shader, once, when a target is first used (the AspectRatio setting can bring one in during the game)
void SuperAA::CreateShader()
{
	if (m_vao) {
		return;
	}

	{
		static const char* vertexShader = R"glsl(

			#version 410 core

			void main(void)
			{
				const vec4 vertices[] = vec4[](vec4(-1.0, -1.0, 0.0, 1.0),
												vec4(-1.0,  1.0, 0.0, 1.0),
												vec4( 1.0, -1.0, 0.0, 1.0),
												vec4( 1.0,  1.0, 0.0, 1.0));

				gl_Position = vertices[gl_VertexID % 4];
			}

			)glsl";


		static const std::string fragmentShaderVersion = R"glsl(
			#version 410 core

		)glsl";

		std::string aaString = "const int aa = ";
		aaString += std::to_string(m_aa);
		aaString += ";\n";
		std::string ccString = "#define CRTCOLORS ";
		ccString += std::to_string((int)m_crtcolors);
		ccString += '\n';

		static const std::string fragmentShader = R"glsl(

		// inputs
		uniform sampler2D tex1;			// base tex

		// outputs
		out vec4 fragColor;

		#if (CRTCOLORS == 1)
		const float cgamma = 2.5; // what is the 'real' gamma/EOTF of a japanese arcade CRT of that time? 2.4 or 2.5 is what the net so far agrees on
		#elif (CRTCOLORS == 2)
		const float cgamma = 2.25;
		#elif (CRTCOLORS > 2)
		const float cgamma = -1.0;
		#endif

		// see https://www.retrorgb.com/colour-malarkey.html
		// and accompanying matrices https://github.com/danmons/colour_matrix_adaptations/tree/main/csv
		#if (CRTCOLORS == 1)
		const mat3 colmatrix = mat3(1.3272128334714093, // ARI(D93/9300K) (JP) -> sRGB(D65/6500K) // what can be found in some of the sega or corresponding CRT manuals of the time is a range of ~9700K-10600K, so even cooler than D93
									-0.3802108879412366,
									-0.1003607696463202,
									-0.0241848600268417,
									0.9544506228550125,
									0.0807358585208939,
									-0.0239585810555497,
									-0.0409736005706461,
									1.4076563728858242);
		#elif (CRTCOLORS == 2)
		const mat3 colmatrix = mat3(0.9241392201737613, // PVM_20M2U(D93/9300K) (JP) -> sRGB(D65/6500K), gamma 2.25
									-0.0690701363506526,
									-0.0084279079392561,
									0.0231962420140721,
									0.9729221778325590,
									0.0148832015024337,
									-0.0054875731998806,
									0.0098220960740544,
									1.3383896683854550);
		#elif (CRTCOLORS == 3)
		const mat3 colmatrix = mat3(0.7822490684754086, // BT601_525_D93 (JP) -> sRGB(D65/6500K), gamma sRGB
									0.0506168576073398,
									0.0137752498011040,
									0.0147968947158740,
									0.9741745313046067,
									0.0220301953285839,
									-0.0013501205469208,
									-0.0044076726925543,
									1.3484819844991036);
		#elif (CRTCOLORS == 4)
		const mat3 colmatrix = mat3(0.9395420637732393, // BT601_525 (US) -> sRGB(D65/6500K), gamma sRGB // extremely subtle
									0.0501813568598678,
									0.0102765793668928,
									0.0177722231435608,
									0.9657928624969044,
									0.0164349143595347,
									-0.0016215999431855,
									-0.0043697496597356,
									1.0059913496029214);
		#elif (CRTCOLORS == 5)
		const mat3 colmatrix = mat3(1.0440432087628346, // BT601_625 (EUR,AUS) -> sRGB(D65/6500K), gamma sRGB // barely noticeable at all
									-0.0440432087628348,
									0.0000000000000001,
									0.0000000000000000,
									1.0000000000000002,
									0.0000000000000000,
									0.0000000000000000,
									0.0117933782840052,
									0.9882066217159947);
		#endif

		vec3 GetTextureValue(sampler2D s)
		{
			ivec2 texPos	= ivec2(gl_FragCoord.xy /*-vec2(0.5)*/) * aa;
			vec3 texColour	= vec3(0.0);

			for(int i=0; i < aa; i++) {
				for(int j=0; j < aa; j++) {
					texColour += texelFetch(s,ivec2(texPos.x+i,texPos.y+j),0).rgb;
				}
			}

			texColour *= 1.0/float(aa * aa);

			return texColour;
		}

		float sRGB(float color)
		{
			if (color <= 0.0031308)
				return 12.92 * color;
			else
				return 1.055 * pow(color, 1.0/2.4) - 0.055;
		}

		float invsRGB(float color)
		{
			if (color <= 0.04045) // 0.03928 ?
				return color * (1.0/12.92);
			else
				return pow(color * (1.0/1.055) + (0.055/1.055), 2.4);
		}

		// The AspectRatio setting: each window pixel from the image in its frame (see AspectRatio.cpp)
		uniform int present;
		uniform vec2 windowSize;
		uniform vec4 frameRect;			// window pixels, origin at the top left
		uniform vec4 imageRect;
		uniform vec4 imageInTarget;		// the image in the texture (0..1, origin at the bottom left)
		uniform int barrelAxis;			// curved edges: 0 horizontal, 1 vertical, -1 none
		uniform vec4 barrel1;			// edge, center, start, scale
		uniform vec4 barrel2;			// length before, after, angle before, after
		uniform int samples;			// per axis and pixel

		float EdgeArc(float x, float len, float angle, float edge)
		{
			if (len <= edge * 1.0001 || angle < 1e-4)
				return len * x / edge;
			return len * asin(sin(angle) * x / edge) / angle;
		}

		float Barrel(float q)
		{
			float u;
			if (q < barrel1.x)
				u = barrel1.z - EdgeArc(barrel1.x - q, barrel2.x, barrel2.z, barrel1.x);
			else if (q > barrel1.x + barrel1.y)
				u = barrel1.z + barrel1.y + EdgeArc(q - barrel1.x - barrel1.y, barrel2.y, barrel2.w, barrel1.x);
			else
				u = barrel1.z + q - barrel1.x;
			return u * barrel1.w;
		}

		vec3 PresentSample(sampler2D s, vec2 p)
		{
			vec2 q = (p - frameRect.xy) / frameRect.zw;
			if (any(lessThan(q, vec2(0.0))) || any(greaterThan(q, vec2(1.0))))
				return vec3(0.0);
			vec2 uv = (p - imageRect.xy) / imageRect.zw;
			if (barrelAxis == 0)
				uv.x = Barrel(q.x);
			else if (barrelAxis == 1)
				uv.y = Barrel(q.y);
			vec2 t = imageInTarget.xy + vec2(uv.x, 1.0 - uv.y) * imageInTarget.zw;
			if (any(lessThan(t, vec2(0.0))) || any(greaterThan(t, vec2(1.0))))
				return vec3(0.0);
			return textureLod(s, t, 0.0).rgb;
		}

		vec3 Present(sampler2D s)
		{
			vec3 texColour = vec3(0.0);
			for (int i = 0; i < samples; i++) {
				for (int j = 0; j < samples; j++) {
					vec2 sub = (vec2(i, j) + 0.5) / float(samples);
					texColour += PresentSample(s, vec2(gl_FragCoord.x - 0.5 + sub.x, windowSize.y - (gl_FragCoord.y - 0.5 + sub.y)));
				}
			}
			return texColour / float(samples * samples);
		}

		void main()
		{
			vec3 finalColor = present != 0 ? Present(tex1) : GetTextureValue(tex1);

			#if (CRTCOLORS != 0)
			{
				// transform input space to linear space
				if (cgamma != -1.0)
					finalColor = pow(finalColor, vec3(cgamma));
				else
					finalColor = vec3(invsRGB(finalColor.r),invsRGB(finalColor.g),invsRGB(finalColor.b));
				// do color transformation
				finalColor *= colmatrix;
				// transform from linear to sRGB output space
				finalColor = vec3(sRGB(finalColor.r),sRGB(finalColor.g),sRGB(finalColor.b));
			}
			#endif

			fragColor = vec4(finalColor, 1.0);
		}

		)glsl";

		std::string fragmentShaderString = fragmentShaderVersion + aaString + ccString + fragmentShader;

		// load shaders
		m_shader.LoadShaders(vertexShader, fragmentShaderString.c_str());
		m_shader.GetUniformLocationMap("tex1");
		m_locPresent		= m_shader.GetUniformLocation("present");
		m_locWindowSize		= m_shader.GetUniformLocation("windowSize");
		m_locFrame			= m_shader.GetUniformLocation("frameRect");
		m_locImage			= m_shader.GetUniformLocation("imageRect");
		m_locImageInTarget	= m_shader.GetUniformLocation("imageInTarget");
		m_locBarrelAxis		= m_shader.GetUniformLocation("barrelAxis");
		m_locBarrel1		= m_shader.GetUniformLocation("barrel1");
		m_locBarrel2		= m_shader.GetUniformLocation("barrel2");
		m_locSamples		= m_shader.GetUniformLocation("samples");

		// setup uniform memory
		m_shader.EnableShader();
		glUniform1i(m_shader.attribLocMap["tex1"], 0);		// texture will be bound to unit zero
		glUniform1i(m_locPresent, 0);
		m_shader.DisableShader();

		glGenVertexArrays(1, &m_vao);
		glBindVertexArray(m_vao);
		// no states needed since we do it in the shader
		glBindVertexArray(0);
	}
}

// need an active context bound to the current thread to destroy our objects
SuperAA::~SuperAA()
{
	m_shader.UnloadShaders();
	m_fbo.Destroy();
	m_windowFbo.Destroy();

	if (m_vao) {
		glDeleteVertexArrays(1, &m_vao);
		m_vao = 0;
	}
}

bool SuperAA::UsesTarget() const
{
	return (m_aa > 1) || (m_crtcolors != CRTcolor::None) || m_forceTarget || m_present;
}

void SuperAA::SetPresentation(const AspectRatio::Layout *layout, int windowWidth, int windowHeight)
{
	m_present = layout != nullptr;
	if (layout) {
		m_layout = *layout;
	}
	m_windowWidth = windowWidth;
	m_windowHeight = windowHeight;
}

void SuperAA::Init(int width, int height)
{
	m_windowFbo.Destroy();
	if (UsesTarget()) {
		CreateShader();
		m_fbo.Destroy();
		m_fbo.Create(width * m_aa, height * m_aa, m_depthStencil);
		if (m_present && m_forceTarget) {
			m_windowFbo.Create(m_windowWidth, m_windowHeight);
		}

		m_width = width;
		m_height = height;
	}
	else {
		m_fbo.Destroy();	// straight on the back buffer again (a presentation removed)
	}
}

void SuperAA::Draw()
{
	if (UsesTarget()) {
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_BLEND);

		glBindTexture(GL_TEXTURE_2D, m_fbo.GetTextureID());
		glBindVertexArray(m_vao);
		m_shader.EnableShader();
		if (m_present) {
			const AspectRatio::Layout& l = m_layout;
			float tw = float(l.targetWidth), th = float(l.targetHeight);
			glViewport(0, 0, m_windowWidth, m_windowHeight);
			glUniform1i(m_locPresent, 1);
			glUniform2f(m_locWindowSize, float(m_windowWidth), float(m_windowHeight));
			glUniform4f(m_locFrame, l.frame[0], l.frame[1], l.frame[2], l.frame[3]);
			glUniform4f(m_locImage, l.image[0], l.image[1], l.image[2], l.image[3]);
			glUniform4f(m_locImageInTarget, l.imageX / tw, l.imageY / th, l.imageWidth / tw, l.imageHeight / th);
			glUniform1i(m_locBarrelAxis, l.barrelAxis);
			glUniform4f(m_locBarrel1, l.edge, l.center, l.start, l.scale);
			glUniform4f(m_locBarrel2, l.length[0], l.length[1], l.angle[0], l.angle[1]);
			glUniform1i(m_locSamples, std::max(m_aa, 2));	// the image is scaled: a few filtered samples per pixel
		}
		else {
			glViewport(0, 0, m_width, m_height);
			glUniform1i(m_locPresent, 0);
		}
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		if (m_present && m_windowFbo.GetFBOID()) {
			// Again into the agent's copy of the window, left bound for what is drawn over it (the in-game menu)
			glBindFramebuffer(GL_FRAMEBUFFER, m_windowFbo.GetFBOID());
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
		m_shader.DisableShader();
		glBindVertexArray(0);
	}
}

GLuint SuperAA::GetWindowTargetID()
{
	return m_windowFbo.GetFBOID();
}

GLuint SuperAA::GetTargetID()
{
	return m_fbo.GetFBOID();	// will return 0 if no render target which will be our default frame buffer (back buffer)
}
