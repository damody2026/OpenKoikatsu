//! CPU replay of Koikano/create_skinface 0001 DXBC. Images in this API use
//! readback rows (top row = Unity v=1), independently of asset decoder rows.
use crate::unity::TextureSampling;
use image::RgbaImage;

pub(crate) struct Texture<'a> {
    pub image: &'a RgbaImage,
    pub sampling: TextureSampling,
    pub st: [f32; 4],
}

impl Texture<'_> {
    fn sample(&self, uv: [f32; 2]) -> [f32; 4] {
        let u = uv[0] * self.st[0] + self.st[2];
        let v = uv[1] * self.st[1] + self.st[3];
        super::sample_clothes_mask(self.image, u, 1. - v, self.sampling)
    }
}

pub(crate) struct Inputs<'a> {
    pub main: Texture<'a>,
    pub mask: Option<Texture<'a>>,
    pub cheek: Option<Texture<'a>>,
    pub lipline: Option<Texture<'a>>,
    pub paint: [Option<Texture<'a>>; 2],
    pub paint_mask: Option<Texture<'a>>,
    pub mole: Option<Texture<'a>>,
    // Unity Color properties are converted to linear RGB, independently of
    // each texture's imported sRGB bit. Alpha is never gamma-converted.
    pub colors: [[f32; 4]; 7],
    pub cheek_uv: [f32; 4],
    pub lipline_uv: [f32; 4],
    pub paint_uv: [[f32; 4]; 2],
    pub mole_uv: [f32; 4],
}

fn scale_uv(uv: [f32; 2], value: [f32; 4]) -> [f32; 2] {
    std::array::from_fn(|i| (uv[i] + value[i]) * (1. - value[i + 2])
                        + value[i + 2] - 0.5)
}

fn layout_uv(uv: [f32; 2], value: [f32; 4]) -> [f32; 2] {
    let scale = 4. * (1. - value[3]);
    let x = (uv[0] + value[0]) * scale - scale * 0.5;
    let y = (uv[1] + value[1]) * scale - scale * 0.5;
    let (sin, cos) = (value[2] * std::f32::consts::TAU).sin_cos();
    [x * cos + y * sin + 0.5, -x * sin + y * cos + 0.5]
}

fn blend(base: &mut [f32; 4], texture: &Option<Texture<'_>>, uv: [f32; 2],
         color: [f32; 4], mask: f32) {
    let Some(texture) = texture else { return };
    let sample = texture.sample(uv);
    let alpha = sample[3] * color[3] * mask;
    for c in 0..4 {
        base[c] += alpha * (sample[c] * color[c] - base[c]);
    }
}

pub(crate) fn bake(inputs: &Inputs<'_>, width: u32, height: u32) -> RgbaImage {
    let colors: [[f32; 4]; 7] = inputs.colors.map(|color| std::array::from_fn(|c|
        if c < 3 { super::srgb_channel_to_linear(color[c]) } else { color[c] }));
    RgbaImage::from_fn(width, height, |x, y| {
        let uv = [(x as f32 + 0.5) / width as f32, 1. - (y as f32 + 0.5) / height as f32];
        let mask = inputs.mask.as_ref().map(|t| t.sample(uv)).unwrap_or([1.; 4]);
        let source = inputs.main.sample(uv);
        let mut value = std::array::from_fn(|c|
            source[c] * ((1. + mask[0] * (colors[0][c] - 1.))
                       * (1. + mask[1] * (colors[1][c] - 1.))).max(mask[2]));
        blend(&mut value, &inputs.cheek, scale_uv(uv, inputs.cheek_uv), colors[3], 1.);
        blend(&mut value, &inputs.lipline, scale_uv(uv, inputs.lipline_uv), colors[4], 1.);
        let paint_mask = inputs.paint_mask.as_ref().map(|t| t.sample(uv)[0]).unwrap_or(1.);
        blend(&mut value, &inputs.paint[0], layout_uv(uv, inputs.paint_uv[0]), colors[2], paint_mask);
        blend(&mut value, &inputs.paint[1], layout_uv(uv, inputs.paint_uv[1]), colors[6], paint_mask);
        blend(&mut value, &inputs.mole, layout_uv(uv, inputs.mole_uv), colors[5], 1.);
        image::Rgba(std::array::from_fn(|c| super::to_unorm8(
            if c < 3 { super::linear_channel_to_srgb(value[c]) } else { value[c] })))
    })
}
