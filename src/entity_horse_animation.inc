// Generated from the original ModelHorse.setLivingAnimations equations.
void animateHorse(Geometry& g,const EntityPose& p) {
float f3 = p.headYaw * (180.0f / pi);
        float f4 = p.headPitch;

        if (f3 > 20.0f)
        {
            f3 = 20.0f;
        }

        if (f3 < -20.0f)
        {
            f3 = -20.0f;
        }

        if (p.limbSwingAmount > 0.2f)
        {
            f4 += animCos(p.limbSwing * 0.4f) * 0.15f * p.limbSwingAmount;
        }


        float f5 = p.grazing;
        float f6 = p.rearing;
        float f7 = 1.0f - f6;
        float f8 = p.mouthOpen;
        bool flag = (p.flags & PoseTailWag) != 0;
        bool flag1 = (p.flags & PoseSaddled) != 0;
        bool flag2 = (p.flags & PoseHasRider) != 0;
        float f9 = p.ageTicks;
        float f10 = animCos(p.limbSwing * 0.6662f + pi);
        float f11 = f10 * 0.8f * p.limbSwingAmount;
        g.named("head").pivot.y = 4.0f;
        g.named("head").pivot.z = -10.0f;
        g.named("tailBase").pivot.y = 3.0f;
        g.named("tailMiddle").pivot.z = 14.0f;
        g.named("muleRightChest").pivot.y = 3.0f;
        g.named("muleRightChest").pivot.z = 10.0f;
        g.named("body").angle.x = 0.0f;
        g.named("head").angle.x = 0.5235988f + f4;
        g.named("head").angle.y = f3 / (180.0f / pi);
        g.named("head").angle.x = f6 * (0.2617994f + f4) + f5 * 2.18166f + (1.0f - std::max(f6, f5)) * g.named("head").angle.x;
        g.named("head").angle.y = f6 * f3 / (180.0f / pi) + (1.0f - std::max(f6, f5)) * g.named("head").angle.y;
        g.named("head").pivot.y = f6 * -6.0f + f5 * 11.0f + (1.0f - std::max(f6, f5)) * g.named("head").pivot.y;
        g.named("head").pivot.z = f6 * -1.0f + f5 * -10.0f + (1.0f - std::max(f6, f5)) * g.named("head").pivot.z;
        g.named("tailBase").pivot.y = f6 * 9.0f + f7 * g.named("tailBase").pivot.y;
        g.named("tailMiddle").pivot.z = f6 * 18.0f + f7 * g.named("tailMiddle").pivot.z;
        g.named("muleRightChest").pivot.y = f6 * 5.5f + f7 * g.named("muleRightChest").pivot.y;
        g.named("muleRightChest").pivot.z = f6 * 15.0f + f7 * g.named("muleRightChest").pivot.z;
        g.named("body").angle.x = f6 * -45.0f / (180.0f / pi) + f7 * g.named("body").angle.x;
        g.named("horseLeftEar").pivot.y = g.named("head").pivot.y;
        g.named("horseRightEar").pivot.y = g.named("head").pivot.y;
        g.named("muleLeftEar").pivot.y = g.named("head").pivot.y;
        g.named("muleRightEar").pivot.y = g.named("head").pivot.y;
        g.named("neck").pivot.y = g.named("head").pivot.y;
        g.named("field_178711_b").pivot.y = 0.02f;
        g.named("field_178712_c").pivot.y = 0.0f;
        g.named("mane").pivot.y = g.named("head").pivot.y;
        g.named("horseLeftEar").pivot.z = g.named("head").pivot.z;
        g.named("horseRightEar").pivot.z = g.named("head").pivot.z;
        g.named("muleLeftEar").pivot.z = g.named("head").pivot.z;
        g.named("muleRightEar").pivot.z = g.named("head").pivot.z;
        g.named("neck").pivot.z = g.named("head").pivot.z;
        g.named("field_178711_b").pivot.z = 0.02f - f8 * 1.0f;
        g.named("field_178712_c").pivot.z = 0.0f + f8 * 1.0f;
        g.named("mane").pivot.z = g.named("head").pivot.z;
        g.named("horseLeftEar").angle.x = g.named("head").angle.x;
        g.named("horseRightEar").angle.x = g.named("head").angle.x;
        g.named("muleLeftEar").angle.x = g.named("head").angle.x;
        g.named("muleRightEar").angle.x = g.named("head").angle.x;
        g.named("neck").angle.x = g.named("head").angle.x;
        g.named("field_178711_b").angle.x = 0.0f - 0.09424778f * f8;
        g.named("field_178712_c").angle.x = 0.0f + 0.15707964f * f8;
        g.named("mane").angle.x = g.named("head").angle.x;
        g.named("horseLeftEar").angle.y = g.named("head").angle.y;
        g.named("horseRightEar").angle.y = g.named("head").angle.y;
        g.named("muleLeftEar").angle.y = g.named("head").angle.y;
        g.named("muleRightEar").angle.y = g.named("head").angle.y;
        g.named("neck").angle.y = g.named("head").angle.y;
        g.named("field_178711_b").angle.y = 0.0f;
        g.named("field_178712_c").angle.y = 0.0f;
        g.named("mane").angle.y = g.named("head").angle.y;
        g.named("muleLeftChest").angle.x = f11 / 5.0f;
        g.named("muleRightChest").angle.x = -f11 / 5.0f;
        float f12 = (pi / 2.0f);


        float f15 = 0.2617994f * f6;
        float f16 = animCos(f9 * 0.6f + pi);
        g.named("frontLeftLeg").pivot.y = -2.0f * f6 + 9.0f * f7;
        g.named("frontLeftLeg").pivot.z = -2.0f * f6 + -8.0f * f7;
        g.named("frontRightLeg").pivot.y = g.named("frontLeftLeg").pivot.y;
        g.named("frontRightLeg").pivot.z = g.named("frontLeftLeg").pivot.z;
        g.named("backLeftShin").pivot.y = g.named("backLeftLeg").pivot.y + animSin((pi / 2.0f) + f15 + f7 * -f10 * 0.5f * p.limbSwingAmount) * 7.0f;
        g.named("backLeftShin").pivot.z = g.named("backLeftLeg").pivot.z + animCos((pi * 3.0f / 2.0f) + f15 + f7 * -f10 * 0.5f * p.limbSwingAmount) * 7.0f;
        g.named("backRightShin").pivot.y = g.named("backRightLeg").pivot.y + animSin((pi / 2.0f) + f15 + f7 * f10 * 0.5f * p.limbSwingAmount) * 7.0f;
        g.named("backRightShin").pivot.z = g.named("backRightLeg").pivot.z + animCos((pi * 3.0f / 2.0f) + f15 + f7 * f10 * 0.5f * p.limbSwingAmount) * 7.0f;
        float f17 = (-1.0471976f + f16) * f6 + f11 * f7;
        float f18 = (-1.0471976f + -f16) * f6 + -f11 * f7;
        g.named("frontLeftShin").pivot.y = g.named("frontLeftLeg").pivot.y + animSin((pi / 2.0f) + f17) * 7.0f;
        g.named("frontLeftShin").pivot.z = g.named("frontLeftLeg").pivot.z + animCos((pi * 3.0f / 2.0f) + f17) * 7.0f;
        g.named("frontRightShin").pivot.y = g.named("frontRightLeg").pivot.y + animSin((pi / 2.0f) + f18) * 7.0f;
        g.named("frontRightShin").pivot.z = g.named("frontRightLeg").pivot.z + animCos((pi * 3.0f / 2.0f) + f18) * 7.0f;
        g.named("backLeftLeg").angle.x = f15 + -f10 * 0.5f * p.limbSwingAmount * f7;
        g.named("backLeftShin").angle.x = -0.08726646f * f6 + (-f10 * 0.5f * p.limbSwingAmount - std::max(0.0f, f10 * 0.5f * p.limbSwingAmount)) * f7;
        g.named("backLeftHoof").angle.x = g.named("backLeftShin").angle.x;
        g.named("backRightLeg").angle.x = f15 + f10 * 0.5f * p.limbSwingAmount * f7;
        g.named("backRightShin").angle.x = -0.08726646f * f6 + (f10 * 0.5f * p.limbSwingAmount - std::max(0.0f, -f10 * 0.5f * p.limbSwingAmount)) * f7;
        g.named("backRightHoof").angle.x = g.named("backRightShin").angle.x;
        g.named("frontLeftLeg").angle.x = f17;
        g.named("frontLeftShin").angle.x = (g.named("frontLeftLeg").angle.x + pi * std::max(0.0f, 0.2f + f16 * 0.2f)) * f6 + (f11 + std::max(0.0f, f10 * 0.5f * p.limbSwingAmount)) * f7;
        g.named("frontLeftHoof").angle.x = g.named("frontLeftShin").angle.x;
        g.named("frontRightLeg").angle.x = f18;
        g.named("frontRightShin").angle.x = (g.named("frontRightLeg").angle.x + pi * std::max(0.0f, 0.2f - f16 * 0.2f)) * f6 + (-f11 + std::max(0.0f, -f10 * 0.5f * p.limbSwingAmount)) * f7;
        g.named("frontRightHoof").angle.x = g.named("frontRightShin").angle.x;
        g.named("backLeftHoof").pivot.y = g.named("backLeftShin").pivot.y;
        g.named("backLeftHoof").pivot.z = g.named("backLeftShin").pivot.z;
        g.named("backRightHoof").pivot.y = g.named("backRightShin").pivot.y;
        g.named("backRightHoof").pivot.z = g.named("backRightShin").pivot.z;
        g.named("frontLeftHoof").pivot.y = g.named("frontLeftShin").pivot.y;
        g.named("frontLeftHoof").pivot.z = g.named("frontLeftShin").pivot.z;
        g.named("frontRightHoof").pivot.y = g.named("frontRightShin").pivot.y;
        g.named("frontRightHoof").pivot.z = g.named("frontRightShin").pivot.z;

        if (flag1)
        {
            g.named("horseSaddleBottom").pivot.y = f6 * 0.5f + f7 * 2.0f;
            g.named("horseSaddleBottom").pivot.z = f6 * 11.0f + f7 * 2.0f;
            g.named("horseSaddleFront").pivot.y = g.named("horseSaddleBottom").pivot.y;
            g.named("horseSaddleBack").pivot.y = g.named("horseSaddleBottom").pivot.y;
            g.named("horseLeftSaddleRope").pivot.y = g.named("horseSaddleBottom").pivot.y;
            g.named("horseRightSaddleRope").pivot.y = g.named("horseSaddleBottom").pivot.y;
            g.named("horseLeftSaddleMetal").pivot.y = g.named("horseSaddleBottom").pivot.y;
            g.named("horseRightSaddleMetal").pivot.y = g.named("horseSaddleBottom").pivot.y;
            g.named("muleLeftChest").pivot.y = g.named("muleRightChest").pivot.y;
            g.named("horseSaddleFront").pivot.z = g.named("horseSaddleBottom").pivot.z;
            g.named("horseSaddleBack").pivot.z = g.named("horseSaddleBottom").pivot.z;
            g.named("horseLeftSaddleRope").pivot.z = g.named("horseSaddleBottom").pivot.z;
            g.named("horseRightSaddleRope").pivot.z = g.named("horseSaddleBottom").pivot.z;
            g.named("horseLeftSaddleMetal").pivot.z = g.named("horseSaddleBottom").pivot.z;
            g.named("horseRightSaddleMetal").pivot.z = g.named("horseSaddleBottom").pivot.z;
            g.named("muleLeftChest").pivot.z = g.named("muleRightChest").pivot.z;
            g.named("horseSaddleBottom").angle.x = g.named("body").angle.x;
            g.named("horseSaddleFront").angle.x = g.named("body").angle.x;
            g.named("horseSaddleBack").angle.x = g.named("body").angle.x;
            g.named("horseLeftRein").pivot.y = g.named("head").pivot.y;
            g.named("horseRightRein").pivot.y = g.named("head").pivot.y;
            g.named("horseFaceRopes").pivot.y = g.named("head").pivot.y;
            g.named("horseLeftFaceMetal").pivot.y = g.named("head").pivot.y;
            g.named("horseRightFaceMetal").pivot.y = g.named("head").pivot.y;
            g.named("horseLeftRein").pivot.z = g.named("head").pivot.z;
            g.named("horseRightRein").pivot.z = g.named("head").pivot.z;
            g.named("horseFaceRopes").pivot.z = g.named("head").pivot.z;
            g.named("horseLeftFaceMetal").pivot.z = g.named("head").pivot.z;
            g.named("horseRightFaceMetal").pivot.z = g.named("head").pivot.z;
            g.named("horseLeftRein").angle.x = f4;
            g.named("horseRightRein").angle.x = f4;
            g.named("horseFaceRopes").angle.x = g.named("head").angle.x;
            g.named("horseLeftFaceMetal").angle.x = g.named("head").angle.x;
            g.named("horseRightFaceMetal").angle.x = g.named("head").angle.x;
            g.named("horseFaceRopes").angle.y = g.named("head").angle.y;
            g.named("horseLeftFaceMetal").angle.y = g.named("head").angle.y;
            g.named("horseLeftRein").angle.y = g.named("head").angle.y;
            g.named("horseRightFaceMetal").angle.y = g.named("head").angle.y;
            g.named("horseRightRein").angle.y = g.named("head").angle.y;

            if (flag2)
            {
                g.named("horseLeftSaddleRope").angle.x = -1.0471976f;
                g.named("horseLeftSaddleMetal").angle.x = -1.0471976f;
                g.named("horseRightSaddleRope").angle.x = -1.0471976f;
                g.named("horseRightSaddleMetal").angle.x = -1.0471976f;
                g.named("horseLeftSaddleRope").angle.z = 0.0f;
                g.named("horseLeftSaddleMetal").angle.z = 0.0f;
                g.named("horseRightSaddleRope").angle.z = 0.0f;
                g.named("horseRightSaddleMetal").angle.z = 0.0f;
            }
            else
            {
                g.named("horseLeftSaddleRope").angle.x = f11 / 3.0f;
                g.named("horseLeftSaddleMetal").angle.x = f11 / 3.0f;
                g.named("horseRightSaddleRope").angle.x = f11 / 3.0f;
                g.named("horseRightSaddleMetal").angle.x = f11 / 3.0f;
                g.named("horseLeftSaddleRope").angle.z = f11 / 5.0f;
                g.named("horseLeftSaddleMetal").angle.z = f11 / 5.0f;
                g.named("horseRightSaddleRope").angle.z = -f11 / 5.0f;
                g.named("horseRightSaddleMetal").angle.z = -f11 / 5.0f;
            }
        }

        f12 = -1.3089f + p.limbSwingAmount * 1.5f;

        if (f12 > 0.0f)
        {
            f12 = 0.0f;
        }

        if (flag)
        {
            g.named("tailBase").angle.y = animCos(f9 * 0.7f);
            f12 = 0.0f;
        }
        else
        {
            g.named("tailBase").angle.y = 0.0f;
        }

        g.named("tailMiddle").angle.y = g.named("tailBase").angle.y;
        g.named("tailTip").angle.y = g.named("tailBase").angle.y;
        g.named("tailMiddle").pivot.y = g.named("tailBase").pivot.y;
        g.named("tailTip").pivot.y = g.named("tailBase").pivot.y;
        g.named("tailMiddle").pivot.z = g.named("tailBase").pivot.z;
        g.named("tailTip").pivot.z = g.named("tailBase").pivot.z;
        g.named("tailBase").angle.x = f12;
        g.named("tailMiddle").angle.x = f12;
        g.named("tailTip").angle.x = -0.2618f + f12;

}
